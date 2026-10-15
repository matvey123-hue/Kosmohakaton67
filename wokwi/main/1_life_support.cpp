#include "1_life_support.h"
#include <math.h>

// Пороги каждого параметра по сторонам: вниз (холод, сухость, переохлаждение обшивки)
// и вверх (перегрев, сырость). Чужая сторона — ±INFINITY, classify() её не пересечёт.
static const Band LOW_BANDS[LS_PARAM_COUNT] = {
  { LS_AIR_ALARM_LO, LS_AIR_WARN_LO,  INFINITY, INFINITY, LS_AIR_HYST },
  { LS_HUM_ALARM_LO, LS_HUM_WARN_LO,  INFINITY, INFINITY, LS_HUM_HYST },
  { -INFINITY,       LS_HULL_WARN_LO, INFINITY, INFINITY, LS_HULL_HYST },  // АВАРИИ по обшивке нет
};
static const Band HIGH_BANDS[LS_PARAM_COUNT] = {
  { -INFINITY, -INFINITY, LS_AIR_WARN_HI, LS_AIR_ALARM_HI, LS_AIR_HYST },
  { -INFINITY, -INFINITY, LS_HUM_WARN_HI, LS_HUM_ALARM_HI, LS_HUM_HYST },
  { -INFINITY, -INFINITY, INFINITY,       INFINITY,        LS_HULL_HYST },  // сверху у обшивки порогов нет
};

LifeSupport::LifeSupport(const LifeSupportConfig& cfg)
    : cfg(cfg), dht(cfg.pinAir, cfg.airSensorType), oneWire(cfg.pinHull), ds18(&oneWire),
      heater(false), damper(LS_DAMPER_CLOSED_DEG) {}

void LifeSupport::begin() {
  pinMode(cfg.pinHeater, OUTPUT);
  digitalWrite(cfg.pinHeater, cfg.heaterActiveHigh ? LOW : HIGH);
  dht.begin();
  ds18.begin();
  ds18.setWaitForConversion(false);
  hullFound = ds18.getAddress(hullAddr, 0);
  Serial.printf("№1: DS18B20 %s\r\n", hullFound ? "найден" : "не найден");
  damperServo.setPeriodHertz(50);
  damperServo.attach(cfg.pinDamper, cfg.damperMinUs, cfg.damperMaxUs);
}

void LifeSupport::update(uint32_t nowMs) {
  if (nowMs - lastDhtMs >= LS_DHT_READ_MS) {
    lastDhtMs = nowMs;
    readDht(nowMs);
  }
  if (nowMs - lastDs18Ms >= LS_DS18_READ_MS) {
    lastDs18Ms = nowMs;
    readDs18(nowMs);
  }
  driveActuators();
}

// Свежий кадр принудительно (force): у библиотеки свой кэш на 2 с по своим часам, и на
// границе он вернул бы прошлый кадр как новый — тогда один выброс прошёл бы выдержку 2 с.
// Чаще раза в 2 с датчик не опрашиваем — это держит update(). Влажность — из того же кадра.
// Пропал датчик — пишем один раз, пульт показывает последнее значение.
void LifeSupport::readDht(uint32_t nowMs) {
  float t = dht.readTemperature(false, true);
  float h = dht.readHumidity();
  bool ok = !isnan(t) && !isnan(h);
  if (ok != airOk) {
    airOk = ok;
    Serial.println(ok ? "№1: датчик воздуха снова на связи" : "№1: датчик воздуха не отвечает");
  }
  if (!ok) return;
  updateParam(LS_AIR, t, nowMs);
  updateParam(LS_HUM, h, nowMs);
  updateActuators(t);
}

// Без ожидания: requestTemperatures() не блокирует, результат забираем на следующем шаге.
// Читаем по адресу, найденному в begin(): поиск на шине при каждом чтении медленный и капризный.
// Пропал датчик — пишем один раз, пульт показывает последнее значение.
// Температуру считаем сами из scratchpad, а не через getTempC(): у библиотеки −55 °C (нижняя
// граница DS18B20) совпадает с её кодом «отключён», и getTempC() возвращает −127.
void LifeSupport::readDs18(uint32_t nowMs) {
  if (!hullFound) hullFound = ds18.getAddress(hullAddr, 0);
  if (ds18Requested && hullFound) {
    uint8_t sp[9];
    bool ok = ds18.isConnected(hullAddr, sp);  // читает scratchpad и проверяет CRC
    float t = (int16_t)(sp[1] << 8 | sp[0]) / 16.0f;  // даташит: 1/16 °C на единицу
    if (ok != hullOk) {
      hullOk = ok;
      Serial.println(ok ? "№1: DS18B20 снова на связи" : "№1: DS18B20 нет ответа");
    }
    if (ok) updateParam(LS_HULL, t, nowMs);
  }
  ds18.requestTemperatures();
  ds18Requested = true;
}

// Реле и серво трогаем только при смене решения.
void LifeSupport::driveActuators() {
  if (heater != shownHeater) {
    shownHeater = heater;
    digitalWrite(cfg.pinHeater, heater == cfg.heaterActiveHigh ? HIGH : LOW);
    Serial.printf("№1: обогрев %s (воздух %.1f °C)\r\n", heater ? "ВКЛ" : "выкл", airC());
  }
  if (damper != shownDamper) {
    bool first = shownDamper < 0;
    shownDamper = damper;
    damperServo.write(damper);
    if (!first) Serial.printf("№1: заслонка %d° (воздух %.1f °C)\r\n", damper, airC());
  }
}

// Каждая сторона — своя выдержка и свой гистерезис (общие classify() и LevelHold из common.h).
// Возврат с запасом действует только у пересечённого порога: после «тепло» 18,5 °C — уже норма.
// Переход на другую сторону — эстафетой: пока значение там, покинутая сторона держит свой
// уровень и сбрасывается, только когда новая сторона подтвердит свой. Иначе на 2 с между ними
// получилась бы ложная НОРМА при значении вне нормы.
void LifeSupport::updateParam(LsParam i, float v, uint32_t nowMs) {
  ParamState& p = params[i];
  p.value = v;
  p.has = true;
  AlarmLevel lo = classify(v, LOW_BANDS[i], p.low.level);
  AlarmLevel hi = classify(v, HIGH_BANDS[i], p.high.level);
  AlarmLevel lowWas = p.low.level, highWas = p.high.level;
  p.low.update(hi != LVL_OK ? p.low.level : lo, nowMs);
  p.high.update(lo != LVL_OK ? p.high.level : hi, nowMs);
  if (p.high.level != highWas && p.high.level != LVL_OK) p.low = LevelHold();
  if (p.low.level != lowWas && p.low.level != LVL_OK) p.high = LevelHold();
}

// Автореакции по сырому значению, сразу. Зазор между порогами вкл/выкл — свой гистерезис.
void LifeSupport::updateActuators(float airC) {
  if (airC < LS_HEATER_ON_BELOW) heater = true;
  else if (airC >= LS_HEATER_OFF_FROM) heater = false;

  if (airC > LS_DAMPER_OPEN_ABOVE) damper = LS_DAMPER_OPEN_DEG;
  else if (airC <= LS_DAMPER_CLOSE_TO) damper = LS_DAMPER_CLOSED_DEG;
}

AlarmLevel LifeSupport::level() const {
  AlarmLevel lvl = LVL_OK;
  for (auto& p : params) if (p.level() > lvl) lvl = p.level();
  return lvl;
}

AlarmLevel LifeSupport::paramLevel(LsParam p) const { return params[p].level(); }

uint8_t LifeSupport::alarmMask() const {
  uint8_t m = 0;
  for (uint8_t i = 0; i < LS_PARAM_COUNT; i++) {
    if (params[i].low.level == LVL_ALARM) m |= 1 << i;
    if (params[i].high.level == LVL_ALARM) m |= 1 << (i + LS_PARAM_COUNT);
  }
  return m;
}

// Каждый датчик — своей частью строки: молчит воздух — обшивка всё равно в журнале.
void LifeSupport::valueText(char* buf, size_t n) const {
  int len = 0;
  buf[0] = '\0';
  if (hasAir()) len = snprintf(buf, n, "воздух %.1f °C, влажн. %.1f %%", airC(), humidity());
  if (hasHull() && len >= 0 && (size_t)len < n)
    snprintf(buf + len, n - len, "%sобшивка %.1f °C", len ? ", " : "", hullC());
  if (!hasAir() && !hasHull()) snprintf(buf, n, "нет данных");
}

const char* LifeSupport::message() const {
  // Сначала самый тяжёлый параметр; при равенстве — воздух, влажность, обшивка.
  AlarmLevel worst = level();
  if (worst == LVL_OK) return nullptr;
  for (uint8_t i = 0; i < LS_PARAM_COUNT; i++) {
    const ParamState& p = params[i];
    if (p.level() != worst) continue;
    // Сторона — та, на которой подтверждён уровень: вне нормы всегда только одна (эстафета в updateParam).
    bool low = p.low.level != LVL_OK;
    switch (i) {
      case LS_AIR:
        // Исполнитель упоминаем, только если он и правда в этом состоянии: на выдержке
        // после возврата значения в норму уровень ещё держится, а обогрев уже выключен.
        if (worst == LVL_ALARM && low) return heater ? "Холодно! Обогрев включён" : "Холодно в модуле";
        if (worst == LVL_ALARM) return damper == LS_DAMPER_OPEN_DEG ? "Перегрев! Заслонка открыта" : "Перегрев в модуле";
        return low ? "Воздух прохладный" : "Воздух тёплый";
      case LS_HUM:
        if (worst == LVL_ALARM) return low ? "Воздух очень сухой" : "Влажность критическая";
        return low ? "Влажность низкая" : "Влажность высокая";
      case LS_HULL:
        return "Обшивка переохлаждена";
    }
  }
  return nullptr;
}
