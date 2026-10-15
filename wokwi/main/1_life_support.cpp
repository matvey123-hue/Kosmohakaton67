#include "1_life_support.h"
#include <math.h>

// Границы одного параметра. Нет границы — ±INFINITY.
struct Band {
  float alarmLo, warnLo, warnHi, alarmHi;
  float hyst;
};

static const Band BANDS[LS_PARAM_COUNT] = {
  { LS_AIR_ALARM_LO, LS_AIR_WARN_LO, LS_AIR_WARN_HI, LS_AIR_ALARM_HI, LS_AIR_HYST },
  { LS_HUM_ALARM_LO, LS_HUM_WARN_LO, LS_HUM_WARN_HI, LS_HUM_ALARM_HI, LS_HUM_HYST },
  { -INFINITY,       LS_HULL_WARN_LO, INFINITY,      INFINITY,        LS_HULL_HYST },
};

LifeSupport::LifeSupport(const LifeSupportConfig& cfg)
    : cfg(cfg), dht(cfg.pinAir, cfg.airSensorType), oneWire(cfg.pinHull), ds18(&oneWire),
      heater(false), damper(LS_DAMPER_CLOSED_DEG) {
  for (auto& p : params) p = { NAN, false, LVL_OK, LVL_OK, 0 };
}

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

void LifeSupport::readDht(uint32_t nowMs) {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) {
    Serial.println("№1: DHT22 нет ответа");
    return;
  }
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

// Уровень по значению. Гистерезис: пока параметр на уровне cur, граница этого уровня
// сдвинута внутрь нормы на hyst, поэтому дребезг около порога не меняет уровень туда-обратно.
static AlarmLevel classify(float v, const Band& b, AlarmLevel cur) {
  float a = cur == LVL_ALARM ? b.hyst : 0;
  if (v < b.alarmLo + a || v > b.alarmHi - a) return LVL_ALARM;
  float w = cur >= LVL_WARN ? b.hyst : 0;
  if (v < b.warnLo + w || v > b.warnHi - w) return LVL_WARN;
  return LVL_OK;
}

// Ложные тревоги: уровень меняется, только если новое значение держится HOLD_MS.
void LifeSupport::updateParam(LsParam i, float v, uint32_t nowMs) {
  ParamState& p = params[i];
  p.value = v;
  p.has = true;
  AlarmLevel raw = classify(v, BANDS[i], p.level);
  if (raw == p.level) {
    p.candidate = p.level;
    return;
  }
  if (raw != p.candidate) {
    p.candidate = raw;
    p.candidateSinceMs = nowMs;
  } else if (nowMs - p.candidateSinceMs >= HOLD_MS) {
    p.level = raw;
  }
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
  for (auto& p : params) if (p.level > lvl) lvl = p.level;
  return lvl;
}

AlarmLevel LifeSupport::paramLevel(LsParam p) const { return params[p].level; }

uint8_t LifeSupport::alarmMask() const {
  uint8_t m = 0;
  for (uint8_t i = 0; i < LS_PARAM_COUNT; i++)
    if (params[i].level == LVL_ALARM) m |= 1 << i;
  return m;
}

void LifeSupport::valueText(char* buf, size_t n) const {
  if (!hasAir()) snprintf(buf, n, "нет данных");
  else if (!hasHull()) snprintf(buf, n, "воздух %.1f °C, влажн. %.0f %%", airC(), humidity());
  else snprintf(buf, n, "воздух %.1f °C, влажн. %.0f %%, обшивка %.1f °C", airC(), humidity(), hullC());
}

const char* LifeSupport::message() const {
  // Сначала самый тяжёлый параметр; при равенстве — воздух, влажность, обшивка.
  AlarmLevel worst = level();
  if (worst == LVL_OK) return nullptr;
  for (uint8_t i = 0; i < LS_PARAM_COUNT; i++) {
    if (params[i].level != worst) continue;
    // Сторона отклонения — относительно середины нормы: из-за гистерезиса значение
    // уже может быть внутри нормы, а уровень ещё держится.
    bool low = params[i].value < (BANDS[i].warnLo + BANDS[i].warnHi) / 2;
    switch (i) {
      case LS_AIR:
        if (worst == LVL_ALARM) return low ? "Холодно! Обогрев включён" : "Перегрев! Заслонка открыта";
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
