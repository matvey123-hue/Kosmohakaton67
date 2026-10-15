#pragma once
// Подсистема №1 — жизнеобеспечение.
// Класс владеет своим железом: читает датчики воздуха и обшивки, считает уровень тревоги
// и сам управляет реле обогрева и серво заслонки. Про схему (diagram.json) он ничего
// не знает: пины и параметры железа передаёт main.ino в LifeSupportConfig.
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP32Servo.h>
#include "common.h"

// ---- Пороги (по спеке, раздел «Подсистемы») ----
// Воздух, °C: НОРМА 18–26, ВНИМАНИЕ 15–18 или 26–30, АВАРИЯ < 15 или > 30.
const float LS_AIR_ALARM_LO  = 15.0f;
const float LS_AIR_WARN_LO   = 18.0f;
const float LS_AIR_WARN_HI   = 26.0f;
const float LS_AIR_ALARM_HI  = 30.0f;
const float LS_AIR_HYST      = 1.0f;    // возврат на уровень ниже — только с запасом 1 °C
// Влажность, %: НОРМА 30–60, ВНИМАНИЕ 20–30 или 60–80, АВАРИЯ < 20 или > 80.
const float LS_HUM_ALARM_LO  = 20.0f;
const float LS_HUM_WARN_LO   = 30.0f;
const float LS_HUM_WARN_HI   = 60.0f;
const float LS_HUM_ALARM_HI  = 80.0f;
const float LS_HUM_HYST      = 2.0f;
// Обшивка, °C: ВНИМАНИЕ ниже −30, АВАРИИ по обшивке в кейсе нет.
const float LS_HULL_WARN_LO  = -30.0f;
const float LS_HULL_HYST     = 1.0f;

// ---- Автореакции (срабатывают сразу, без выдержки) ----
const float LS_HEATER_ON_BELOW   = 18.0f;  // обогрев вкл при t < 18
const float LS_HEATER_OFF_FROM   = 20.0f;  // выкл при t ≥ 20
const float LS_DAMPER_OPEN_ABOVE = 26.0f;  // заслонка 90° при t > 26
const float LS_DAMPER_CLOSE_TO   = 24.0f;  // 0° при t ≤ 24
const int   LS_DAMPER_OPEN_DEG   = 90;
const int   LS_DAMPER_CLOSED_DEG = 0;

// Параметры подсистемы, у каждого своя цветная метка на пульте.
enum LsParam : uint8_t { LS_AIR, LS_HUM, LS_HULL, LS_PARAM_COUNT };

const uint32_t LS_DHT_READ_MS  = 2000;  // DHT22 чаще раза в 2 с не читают
const uint32_t LS_DS18_READ_MS = 1000;  // DS18B20: запрос, через секунду забираем результат

// Подключение железа подсистемы — заполняет main.ino по схеме.
struct LifeSupportConfig {
  int     pinAir;            // DATA датчика воздуха и влажности
  uint8_t airSensorType;     // тип для библиотеки DHT: DHT22, DHT11…
  int     pinHull;           // шина 1-Wire датчика обшивки (DS18B20)
  int     pinHeater;         // вход реле обогревателя
  bool    heaterActiveHigh;  // true — реле включается уровнем HIGH
  int     pinDamper;         // сигнал серво заслонки
  int     damperMinUs;       // длительность импульса серво для 0°
  int     damperMaxUs;       // и для 180°
};

class LifeSupport : public Subsystem {
public:
  explicit LifeSupport(const LifeSupportConfig& cfg);

  // ---- Subsystem ----
  const char* name() const override { return "№1 Жизнеобеспечение"; }
  void        begin() override;
  void        update(uint32_t nowMs) override;
  AlarmLevel  level() const override;               // максимум по параметрам
  uint8_t     alarmMask() const override;
  const char* message() const override;
  // «воздух 22.0 °C, влажн. 45 %, обшивка -10.0 °C»
  void        valueText(char* buf, size_t n) const override;

  // ---- Подробности для пульта ----
  AlarmLevel  paramLevel(LsParam p) const;    // уровень одного параметра (для метки на пульте)
  float       airC() const      { return params[LS_AIR].value; }
  float       humidity() const  { return params[LS_HUM].value; }
  float       hullC() const     { return params[LS_HULL].value; }
  bool        hasAir() const    { return params[LS_AIR].has; }   // было ли хоть одно показание DHT22
  bool        hasHull() const   { return params[LS_HULL].has; }  // было ли хоть одно показание DS18B20

  // ---- Команды исполнителям ----
  bool        heaterOn() const  { return heater; }   // реле обогревателя
  int         damperDeg() const { return damper; }   // угол серво заслонки

private:
  // Состояние одного параметра: подтверждённый уровень и кандидат, который ждёт 2 с.
  struct ParamState {
    float      value;
    bool       has;
    AlarmLevel level;       // показываем на пульте
    AlarmLevel candidate;   // новый уровень, ещё не продержавшийся HOLD_MS
    uint32_t   candidateSinceMs;
  };

  void readDht(uint32_t nowMs);
  void readDs18(uint32_t nowMs);
  void updateParam(LsParam i, float v, uint32_t nowMs);
  void updateActuators(float airC);
  void driveActuators();

  // Железо. ds18 ссылается на oneWire, поэтому oneWire объявлен раньше.
  const LifeSupportConfig cfg;
  DHT dht;
  OneWire oneWire;
  DallasTemperature ds18;
  Servo damperServo;

  ParamState params[LS_PARAM_COUNT];
  bool heater;
  int  damper;
  uint32_t lastDhtMs = 0, lastDs18Ms = 0;
  bool ds18Requested = false;
  DeviceAddress hullAddr;
  bool hullFound = false;
  bool hullOk = true;        // последнее чтение DS18B20 удалось — для сообщения о пропаже один раз
  bool shownHeater = false;  // что сейчас выставлено на реле и серво
  int  shownDamper = -1;
};
