#include "life_support.h"
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

// Состояние одного параметра: подтверждённый уровень и кандидат, который ждёт 2 с.
struct ParamState {
  float      value;
  bool       has;
  AlarmLevel level;       // показываем на пульте
  AlarmLevel candidate;   // новый уровень, ещё не продержавшийся LS_HOLD_MS
  uint32_t   candidateSinceMs;
};

static ParamState params[LS_PARAM_COUNT];
static bool heaterOn;
static int  damperDeg;

void lifeInit() {
  for (auto& p : params) p = { NAN, false, LVL_OK, LVL_OK, 0 };
  heaterOn = false;
  damperDeg = LS_DAMPER_CLOSED_DEG;
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

// Ложные тревоги: уровень меняется, только если новое значение держится LS_HOLD_MS.
static void updateParam(LsParam i, float v, uint32_t nowMs) {
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
  } else if (nowMs - p.candidateSinceMs >= LS_HOLD_MS) {
    p.level = raw;
  }
}

// Автореакции по сырому значению, сразу. Зазор между порогами вкл/выкл — свой гистерезис.
static void updateActuators(float airC) {
  if (airC < LS_HEATER_ON_BELOW) heaterOn = true;
  else if (airC >= LS_HEATER_OFF_FROM) heaterOn = false;

  if (airC > LS_DAMPER_OPEN_ABOVE) damperDeg = LS_DAMPER_OPEN_DEG;
  else if (airC <= LS_DAMPER_CLOSE_TO) damperDeg = LS_DAMPER_CLOSED_DEG;
}

void lifeUpdateAir(float airC, float humidity, uint32_t nowMs) {
  updateParam(LS_AIR, airC, nowMs);
  updateParam(LS_HUM, humidity, nowMs);
  updateActuators(airC);
}

void lifeUpdateHull(float hullC, uint32_t nowMs) {
  updateParam(LS_HULL, hullC, nowMs);
}

AlarmLevel lifeLevel() {
  AlarmLevel lvl = LVL_OK;
  for (auto& p : params) if (p.level > lvl) lvl = p.level;
  return lvl;
}

AlarmLevel lifeParamLevel(LsParam p) { return params[p].level; }

uint8_t lifeAlarmMask() {
  uint8_t m = 0;
  for (uint8_t i = 0; i < LS_PARAM_COUNT; i++)
    if (params[i].level == LVL_ALARM) m |= 1 << i;
  return m;
}

const char* lifeMessage() {
  // Сначала самый тяжёлый параметр; при равенстве — воздух, влажность, обшивка.
  AlarmLevel worst = lifeLevel();
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

float lifeAirC()      { return params[LS_AIR].value; }
float lifeHumidity()  { return params[LS_HUM].value; }
float lifeHullC()     { return params[LS_HULL].value; }
bool  lifeHasAir()    { return params[LS_AIR].has; }
bool  lifeHasHull()   { return params[LS_HULL].has; }
bool  lifeHeaterOn()  { return heaterOn; }
int   lifeDamperDeg() { return damperDeg; }
