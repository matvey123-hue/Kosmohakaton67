#!/usr/bin/env python3
# Проверка diagram.json против распиновки (спека, раздел 4) и main.ino.
# Использование: python tools/validate.py
# Ноль на выходе молчит, неноль — список проблем.
import json, re, sys, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAIN = os.path.join(ROOT, "wokwi", "main")
diagram = json.load(open(os.path.join(MAIN, "diagram.json"), encoding="utf-8"))
ino = open(os.path.join(MAIN, "main.ino"), encoding="utf-8").read()
errors, warnings = [], []

# ---- Имена пинов деталей (по докам Wokwi и board.json плат) ----
ESP_PINS = {"3V3", "EN", "VP", "VN", "34", "35", "32", "33", "25", "26", "27",
            "14", "12", "GND.1", "13", "D2", "D3", "CMD", "5V", "GND.2", "23",
            "22", "TX", "RX", "21", "GND.3", "19", "18", "5", "17", "16", "4",
            "0", "2", "15", "D1", "D0", "CLK", "$serialMonitor"}
PART_PINS = {
    "board-esp32-devkit-c-v4": ESP_PINS,
    "board-ili9341-cap-touch": {"VCC", "GND", "CS", "RST", "D/C", "MOSI", "SCK",
                                "LED", "MISO", "SCL", "SDA"},
    "wokwi-buzzer": {"1", "2"},
    "wokwi-pushbutton": {"1.l", "1.r", "2.l", "2.r"},
    "wokwi-rgb-led": {"R", "G", "B", "COM"},
    "wokwi-resistor": {"1", "2"},
    "wokwi-dht22": {"VCC", "SDA", "NC", "GND"},
    "wokwi-ds18b20": {"VCC", "DQ", "GND"},
    "wokwi-relay-module": {"VCC", "GND", "IN", "NC", "COM", "NO"},
    "wokwi-led": {"A", "C"},
    "wokwi-servo": {"PWM", "V+", "GND"},
    # У MQ2 аналоговый пин в живом примере Wokwi называется AOUT (в доках — AO).
    "wokwi-gas-sensor": {"VCC", "GND", "DO", "AO", "AOUT"},
    "wokwi-ntc-temperature-sensor": {"VCC", "OUT", "GND"},
    "wokwi-slide-switch": {"1", "2", "3"},
    "board-bmp180": {"VCC", "3.3V", "GND", "SCL", "SDA"},
    "wokwi-mpu6050": {"VCC", "GND", "SCL", "SDA", "XDA", "XCL", "AD0", "INT"},
}
parts = {p["id"]: p["type"] for p in diagram["parts"]}

# 1. Все провода ведут в существующие детали и пины.
for a, b, color, _ in diagram["connections"]:
    for end in (a, b):
        if end.startswith("$"):
            part, pin = "$serialMonitor", end.split(":")[1]
            if pin not in ("RX", "TX"):
                errors.append(f"нет пина {end}")
            continue
        part, pin = end.split(":")
        if part not in parts:
            errors.append(f"нет детали {part} ({end})")
            continue
        known = PART_PINS.get(parts[part])
        if known is None:
            warnings.append(f"неизвестный тип {parts[part]} — пины не проверены")
        elif pin not in known:
            errors.append(f"у {parts[part]} ({part}) нет пина {pin}")

# 2. Ожидаемая распиновка (спека, раздел 4, таблица «GPIO»).
# значение: множество допустимых имён в diagram.json (число или VP/VN).
EXPECTED = {
    "tft:CS": {"5"}, "tft:D/C": {"4"}, "tft:SCK": {"18"}, "tft:MOSI": {"23"},
    "dht:SDA": {"33"}, "ds:DQ": {"2"}, "relayHeat:IN": {"12"}, "damper:PWM": {"15"},
    "doorOuter:2": {"19"}, "doorInner:2": {"VN"},
    "lock:PWM": {"13"}, "bmp:SDA": {"21"}, "bmp:SCL": {"22"},
    "mpu:SDA": {"21"}, "mpu:SCL": {"22"},
    "mq2:AOUT": {"34"}, "ntc:OUT": {"35"}, "relayBay:IN": {"16"}, "btnReset:1.l": {"17"},
    "bz:2": {"25"},
    # RGB идёт через резисторы — проверяем дальний конец резистора.
    "rRgbR:2": {"26"}, "rRgbG:2": {"27"}, "rRgbB:2": {"14"},
    "btnAck:1.l": {"32"},
}
wire = {}
for a, b, color, _ in diagram["connections"]:
    for x, y in ((a, b), (b, a)):
        if x.startswith("esp:") and not y.startswith("esp:") and not y.startswith("$"):
            wire.setdefault(y, set()).add(x.split(":")[1])
for sig, ok in EXPECTED.items():
    got = wire.get(sig, set())
    if got != ok:
        errors.append(f"распиновка: {sig} висит на {sorted(got)}, а надо {sorted(ok)}")

# 3. Нет двух сигналов на одном GPIO (кроме шин питания/земли, TX/RX монитора и I2C).
gpio_use = {}
for a, b, color, _ in diagram["connections"]:
    for x, y in ((a, b), (b, a)):
        if x.startswith("esp:"):
            gpio_use.setdefault(x.split(":")[1], set()).add(y)
SHARED = {"3V3", "5V", "GND.1", "GND.2", "GND.3", "TX", "RX", "21", "22"}
for gpio, users in sorted(gpio_use.items()):
    if len(users) > 1 and gpio not in SHARED:
        errors.append(f"конфликт: esp:{gpio} используют {sorted(users)}")
if wire.get("bmp:SDA") != {"21"} or wire.get("mpu:SDA") != {"21"}:
    pass  # уже проверено в п.2; общая шина I2C разрешена

# 4. Пины из main.ino совпадают с diagram.json.
# В коде — номера GPIO, в схеме VP/VN вместо 36/39: приводим к именам схемы.
code_pins = set(re.findall(r"\.pin\w+\s*=\s*(\d+)", ino))
code_pins |= set(re.findall(r"PIN_\w+\s*=\s*(\d+)", ino))
code_pins = {{"36": "VP", "39": "VN"}.get(p, p) for p in code_pins}
diag_gpio = {g for g in gpio_use if re.fullmatch(r"\d+|VP|VN", g)}
diag_num = {g for g in diag_gpio if re.fullmatch(r"\d+|VP|VN", g)}
if code_pins != diag_num:
    errors.append(f"пины main.ino {sorted(code_pins)} != "
                  f"diagram.json {sorted(diag_num)}")
if "39" in gpio_use:
    errors.append("в схеме есть esp:39 — такого пина нет, надо esp:VN (GPIO39)")
if "36" in gpio_use:
    errors.append("в схеме есть esp:36 — такого пина нет, надо esp:VP (GPIO36)")

# 5. Реле с transistor=pnp (HIGH = замкнуто), иначе инверсия питания.
for p in diagram["parts"]:
    if p["type"] == "wokwi-relay-module" and p["attrs"].get("transistor") != "pnp":
        errors.append(f'{p["id"]}: нет transistor=pnp — реле будет инвертировано')

for w in warnings:
    print("WARN:", w)
for e in errors:
    print("ERR:", e)
print("OK" if not errors else f"FAIL: {len(errors)}")
sys.exit(1 if errors else 0)
