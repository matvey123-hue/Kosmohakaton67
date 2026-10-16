#include "2_fire.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>

void FireSafety::begin() {
  pinMode(cfg.pinRelay, OUTPUT);
  bayPower = true;
  digitalWrite(cfg.pinRelay, cfg.relayActiveHigh ? HIGH : LOW);  // отсек под питанием
  shownRelay = digitalRead(cfg.pinRelay) == HIGH;
  pinMode(cfg.pinReset, INPUT_PULLUP);  // кнопка на GND
  btnStable = digitalRead(cfg.pinReset) == HIGH;
  btnLastRaw = btnStable;
  btnChangeMs = 0;
  Serial.println("№2: питание отсека ВКЛ");
}

float FireSafety::ntcToCelsius(int adc) const {
  // Делитель модуля NTC в Wokwi: 10 кОм сверху (к 3V3), NTC снизу (к GND).
  // Чем выше температура, тем меньше сопротивление NTC и ниже напряжение на OUT.
  if (adc <= 0 || adc >= FIRE_ADC_MAX) return NAN;
  const float v = (float)adc / FIRE_ADC_MAX;          // доля от 3V3
  const float rNtc = FIRE_NTC_RNOM_OHM * v / (1 - v);  // сопротивление NTC
  const float tKelvin = 1.0f / (logf(rNtc / FIRE_NTC_RNOM_OHM) / FIRE_NTC_BETA_C + 1.0f / 298.15f);
  return tKelvin - 273.15f;
}

void FireSafety::readSensors() {
  smoke = analogRead(cfg.pinSmoke);
  temp = ntcToCelsius(analogRead(cfg.pinTemp));
  has = true;
  // Признаки с гистерезисом: назад — только ниже порога на запас,
  // иначе дребезг около порога будет мигать уровнем.
  if (!smokeHi && smoke > FIRE_SMOKE_ADC) smokeHi = true;
  else if (smokeHi && smoke < FIRE_SMOKE_ADC - FIRE_SMOKE_HYST_ADC) smokeHi = false;
  if (!isnan(temp)) {
    if (!tempWarnHi && temp > FIRE_TEMP_WARN_C) tempWarnHi = true;
    else if (tempWarnHi && temp < FIRE_TEMP_WARN_C - FIRE_TEMP_HYST_C) tempWarnHi = false;
    if (!tempAlarmHi && temp > FIRE_TEMP_ALARM_C) tempAlarmHi = true;
    else if (tempAlarmHi && temp < FIRE_TEMP_ALARM_C - FIRE_TEMP_HYST_C) tempAlarmHi = false;
  }
}

// Кнопка «Сброс» возвращает питание отсека, но только из НОРМЫ:
// иначе жюри нажмёт её во время пожара и отсек включится обратно.
// «Сброс» — не подтверждение аварии: она не глушит общий звук, за это отвечает
// кнопка «Подтвердить» в main.ino.
void FireSafety::readButton(uint32_t nowMs) {
  bool raw = digitalRead(cfg.pinReset) == HIGH;  // true — отпущена
  if (raw != btnLastRaw) {
    btnLastRaw = raw;
    btnChangeMs = nowMs;
  }
  if (raw != btnStable && nowMs - btnChangeMs >= FIRE_BUTTON_DEBOUNCE_MS) {
    btnStable = raw;
    if (!raw && hold.level == LVL_OK && !bayPower) {
      bayPower = true;
      Serial.println("№2: СБРОС — питание отсека ВКЛ");
    }
  }
}

void FireSafety::driveRelay() {
  // Питание отсека — через NO-контакт: реле замкнуто — отсек под питанием.
  bool want = bayPower == cfg.relayActiveHigh ? HIGH : LOW;
  if (want != shownRelay) {
    shownRelay = want;
    digitalWrite(cfg.pinRelay, want ? HIGH : LOW);
  }
}

void FireSafety::update(uint32_t nowMs) {
  if (nowMs - lastReadMs >= FIRE_READ_MS) {
    lastReadMs = nowMs;
    readSensors();
  }
  readButton(nowMs);

  // Сырой уровень: АВАРИЯ — оба признака сразу или t > 60;
  // ВНИМАНИЕ — только один признак. Выдержку 2 с даёт общий LevelHold:
  // всплеск дыма или температуры короче HOLD_MS АВАРИЮ не даёт.
  AlarmLevel raw = LVL_OK;
  if (tempAlarmHi || (smokeHi && tempWarnHi)) raw = LVL_ALARM;
  else if (smokeHi || tempWarnHi) raw = LVL_WARN;
  hold.update(raw, nowMs);

  // Автореакция — сразу, без выдержки: при АВАРИИ отсек обесточен.
  if (hold.level == LVL_ALARM && bayPower) {
    bayPower = false;
    Serial.println("№2: АВАРИЯ — питание отсека ВЫКЛ");
  }
  driveRelay();
}

uint8_t FireSafety::alarmMask() const {
  if (hold.level != LVL_ALARM) return 0;
  uint8_t m = 0;
  if (smokeHi && tempWarnHi) m |= 1 << 0;  // дым + перегрев
  if (tempAlarmHi) m |= 1 << 1;            // перегрев выше 60 °C
  return m ? m : 1 << 0;
}

const char* FireSafety::message() const {
  switch (hold.level) {
    case LVL_ALARM:
      if (!bayPower) return "ПОЖАР! Отсек обесточен";
      return "ПОЖАР в отсеке";
    case LVL_WARN:
      if (smokeHi && !tempWarnHi) return "Дым в отсеке";
      if (tempWarnHi && !smokeHi) return "Отсек перегрет";
      return "Подозрение на пожар";
    case LVL_OK:
      break;
  }
  return nullptr;
}

void FireSafety::valueText(char* buf, size_t n) const {
  if (!has) {
    snprintf(buf, n, "нет данных");
    return;
  }
  if (isnan(temp)) snprintf(buf, n, "дым %d АЦП", smoke);
  else snprintf(buf, n, "дым %d АЦП, отсек %.1f C", smoke, (double)temp);
}
