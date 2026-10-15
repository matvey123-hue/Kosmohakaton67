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

// Пороги и автореакции — в common.h (кейс: «пороги в одном месте»).

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
  // Бит на каждую сторону параметра в АВАРИИ: биты 0–2 — ниже нормы (холод, сухость, обшивка),
  // биты 3–4 — выше (перегрев, сырость). Холод → перегрев — новый бит, значит новая авария.
  uint8_t     alarmMask() const override;
  const char* message() const override;
  // «воздух 22.0 °C, влажн. 45.0 %, обшивка -10.0 °C»; датчика без данных в строке нет
  void        valueText(char* buf, size_t n) const override;

  // ---- Подробности для пульта ----
  AlarmLevel  paramLevel(LsParam p) const;    // уровень одного параметра (для метки на пульте)
  float       airC() const      { return params[LS_AIR].value; }
  float       humidity() const  { return params[LS_HUM].value; }
  float       hullC() const     { return params[LS_HULL].value; }
  bool        hasAir() const    { return params[LS_AIR].has; }   // было ли хоть одно показание воздуха
  bool        hasHull() const   { return params[LS_HULL].has; }  // было ли хоть одно показание обшивки

  // ---- Команды исполнителям ----
  bool        heaterOn() const  { return heater; }   // реле обогревателя
  int         damperDeg() const { return damper; }   // угол серво заслонки

private:
  // Состояние одного параметра: последнее значение и уровни с выдержкой 2 с — отдельно
  // для отклонения вниз и вверх. Так гистерезис стоит только у пересечённого порога,
  // а сторона аварии (холод или перегрев) всегда известна. Вне НОРМЫ — не больше одной
  // стороны: покинутая держит уровень, пока другая не подтвердит свой.
  struct ParamState {
    float     value = NAN;
    bool      has = false;
    LevelHold low, high;
    AlarmLevel level() const { return low.level > high.level ? low.level : high.level; }
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
  bool hullOk = true;        // последнее чтение удалось — чтобы писать о пропаже датчика один раз
  bool airOk = true;
  bool shownHeater = false;  // что сейчас выставлено на реле и серво
  int  shownDamper = -1;
};
