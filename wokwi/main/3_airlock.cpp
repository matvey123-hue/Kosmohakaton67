#include "3_airlock.h"
#include <Arduino.h>
#include <stdio.h>

void Airlock::begin() {
  pinMode(cfg.pinDoorOuter, INPUT_PULLUP);  // внешняя: концевик на GND
  pinMode(cfg.pinDoorInner, INPUT);         // внутренняя на 39: своей подтяжки нет,
                                            // slide-switch даёт 3V3/GND сам
  outerRaw = digitalRead(cfg.pinDoorOuter) == LOW;
  innerRaw = digitalRead(cfg.pinDoorInner) == HIGH;
  outer = outerRaw;
  inner = innerRaw;
  bmpFound = bmp.begin();
  Serial.printf("№3: BMP180 %s\r\n", bmpFound ? "найден" : "не найден");
  lockServo.setPeriodHertz(50);
  lockServo.attach(cfg.pinLock, cfg.lockMinUs, cfg.lockMaxUs);
  for (int i = 0; i < DROP_N; i++) {
    dropBuf[i] = NAN;
    dropWhen[i] = 0;
  }
}

// Дверь открыта: внешняя — LOW (концевик на GND), внутренняя — HIGH (slide-switch на 3V3).
// Дребезг давим временем: состояние меняется, только если держится 50 мс.
void Airlock::readDoors(uint32_t nowMs) {
  bool ro = digitalRead(cfg.pinDoorOuter) == LOW;
  if (ro != outerRaw) {
    outerRaw = ro;
    outerChangeMs = nowMs;
  }
  if (ro != outer && nowMs - outerChangeMs >= AIRLOCK_DOOR_DEBOUNCE_MS) outer = ro;

  bool ri = digitalRead(cfg.pinDoorInner) == HIGH;
  if (ri != innerRaw) {
    innerRaw = ri;
    innerChangeMs = nowMs;
  }
  if (ri != inner && nowMs - innerChangeMs >= AIRLOCK_DOOR_DEBOUNCE_MS) inner = ri;
}

void Airlock::readPressure(uint32_t nowMs) {
  if (!bmpFound) {
    // BMP180 нет — пробуем раз в 5 с, вдруг поднимется. Давления нет —
    // считаем его нормой: уровень тогда дают только двери и удар.
    if (nowMs - bmpRetryMs >= 5000) {
      bmpRetryMs = nowMs;
      bmpFound = bmp.begin();
      if (bmpFound) Serial.println("№3: BMP180 снова на связи");
    }
    return;
  }
  float p = bmp.readPressure() / 1000.0f;  // Па → кПа
  bool ok = p >= 30.0f && p <= 110.0f;      // диапазон из кейса
  if (ok != bmpOk) {
    bmpOk = ok;
    Serial.println(ok ? "№3: BMP180 снова на связи" : "№3: BMP180 вне диапазона");
    if (!ok) bmpFound = false;
  }
  if (!ok) return;
  pressure = p;
  has = true;

  // Падение: помним пробы за 10 с, сравниваем максимум с текущим.
  // Кладём не чаще раза в секунду — иначе окно забьётся одним значением.
  if (dropCount == 0 || nowMs - dropWhen[(dropCount - 1) % DROP_N] >= 1000) {
    dropBuf[dropCount % DROP_N] = p;
    dropWhen[dropCount % DROP_N] = nowMs;
    dropCount++;
  }
}

// Серво трогаем только при смене решения.
void Airlock::driveLock() {
  if (lock != shownLock) {
    bool first = shownLock < 0;
    shownLock = lock;
    lockServo.write(lock);
    if (!first)
      Serial.printf("№3: замок %s\r\n", lock == AIRLOCK_LOCK_CLOSED_DEG ? "ЗАКРЫТ" : "открыт");
  }
}

void Airlock::update(uint32_t nowMs) {
  if (nowMs - lastReadMs >= AIRLOCK_READ_MS) {
    lastReadMs = nowMs;
    readPressure(nowMs);
  }
  readDoors(nowMs);  // двери читаем каждый проход: концевик быстрый

  // Давление с гистерезисом: назад из АВАРИИ — только выше 90 + 1 кПа.
  AlarmLevel pressRaw = LVL_OK;
  if (has) {
    static const Band pressBand = { AIRLOCK_ALARM_KPA, AIRLOCK_WARN_KPA,
                                    INFINITY, INFINITY, AIRLOCK_PRESS_HYST_KPA };
    pressRaw = classify(pressure, pressBand, pressHold.level);
    // Падение больше 2 кПа за 10 с — ВНИМАНИЕ, даже если давление ещё в норме.
    float mx = NAN;
    for (int i = 0; i < DROP_N && i < dropCount; i++) {
      uint32_t age = nowMs - dropWhen[(dropCount - 1 - i) % DROP_N];
      if (age <= AIRLOCK_DROP_WINDOW_MS && (isnan(mx) || dropBuf[(dropCount - 1 - i) % DROP_N] > mx))
        mx = dropBuf[(dropCount - 1 - i) % DROP_N];
    }
    if (!isnan(mx) && mx - pressure > AIRLOCK_DROP_KPA && pressRaw == LVL_OK)
      pressRaw = LVL_WARN;
  }
  pressHold.update(pressRaw, nowMs);

  // Обе двери открыты — АВАРИЯ (с выдержкой 2 с: щелчок мимо — не авария).
  doorHold.update(outer && inner ? LVL_ALARM : LVL_OK, nowMs);

  // ВНИМАНИЕ после сильного удара держится 30 с и снимается само.
  impactOn = (int32_t)(impactUntilMs - nowMs) > 0;

  // Автореакция — сразу: внешняя открыта — замок внутренней закрыт.
  // Внутреннюю при этом открыть нельзя: замок держит её дверь.
  lock = outer ? AIRLOCK_LOCK_CLOSED_DEG : AIRLOCK_LOCK_OPEN_DEG;
  driveLock();
}

AlarmLevel Airlock::level() const {
  AlarmLevel lvl = pressHold.level > doorHold.level ? pressHold.level : doorHold.level;
  if (impactOn && lvl < LVL_WARN) lvl = LVL_WARN;
  return lvl;
}

uint8_t Airlock::alarmMask() const {
  uint8_t m = 0;
  if (pressHold.level == LVL_ALARM) m |= 1 << 0;
  if (doorHold.level == LVL_ALARM) m |= 1 << 1;
  return m;
}

const char* Airlock::message() const {
  AlarmLevel lvl = level();
  if (lvl == LVL_OK) return nullptr;
  if (doorHold.level == LVL_ALARM) return "Обе двери открыты!";
  if (pressHold.level == LVL_ALARM) return "Разгерметизация!";
  if (impactOn) return "Удар! Проверить герметичность";
  if (outer) return "Внешняя дверь открыта, замок закрыт";
  if (inner) return "Внутренняя дверь открыта";
  return "Давление падает";
}

void Airlock::valueText(char* buf, size_t n) const {
  char press[24];
  if (has) snprintf(press, sizeof press, "%.1f кПа", (double)pressure);
  else snprintf(press, sizeof press, "нет давл.");
  snprintf(buf, n, "давл. %s, внеш. %s, внутр. %s",
           press, outer ? "ОТКР" : "закр", inner ? "ОТКР" : "закр");
}
