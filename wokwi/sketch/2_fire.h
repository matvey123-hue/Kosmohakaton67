#pragma once
// Подсистема №2 — пожарная безопасность.
// Класс владеет своим железом: читает MQ2 (дым) и NTC (температура отсека),
// считает уровень тревоги и сам управляет реле питания отсека и кнопкой «Сброс».
// Про схему (diagram.json) он ничего не знает: пины передаёт sketch.ino в FireSafetyConfig.
#include "common.h"

// Пороги — в common.h (кейс: «пороги в одном месте»).

// Подключение железа подсистемы — заполняет sketch.ino по схеме.
struct FireSafetyConfig {
  int  pinSmoke;        // MQ2 AOUT (аналоговый вход, ADC1)
  int  pinTemp;         // NTC OUT (аналоговый вход, ADC1)
  int  pinRelay;        // вход реле питания отсека
  bool relayActiveHigh; // true — реле замыкается уровнем HIGH (в Wokwi: атрибут transistor=pnp)
  int  pinReset;        // кнопка «Сброс» на GND
};

class FireSafety : public Subsystem {
public:
  explicit FireSafety(const FireSafetyConfig& cfg) : cfg(cfg) {}

  // ---- Subsystem ----
  const char* name() const override { return "№2 Пожарная безопасность"; }
  void        begin() override;
  void        update(uint32_t nowMs) override;
  AlarmLevel  level() const override { return hold.level; }
  // Биты АВАРИИ: бит 0 — дым и перегрев вместе, бит 1 — перегрев выше 60 °C.
  // Переход «только дым+температура» → «ещё и за 60» — новый бит, значит новая авария.
  uint8_t     alarmMask() const override;
  const char* message() const override;
  // «дым 2100 АЦП, отсек 47.3 °C»; датчика без данных в строке нет
  void        valueText(char* buf, size_t n) const override;

  // ---- Подробности для пульта ----
  int   smokeAdc() const  { return smoke; }
  float tempC() const     { return temp; }
  bool  hasData() const   { return has; }        // было ли хоть одно чтение датчиков
  bool  smokeOver() const { return smokeHi; }    // дым выше порога (с гистерезисом)
  bool  bayPowerOn() const { return bayPower; }  // питание отсека включено (реле замкнуто)

private:
  // NTC из формулы доков Wokwi (делитель 10 кОм + 10 кОм, бета 3950),
  // пересчитанной под АЦП ESP32 0–4095.
  float ntcToCelsius(int adc) const;
  void  readSensors();
  void  readButton(uint32_t nowMs);
  void  driveRelay();

  const FireSafetyConfig cfg;

  LevelHold hold;
  int   smoke = 0;       // последнее показание MQ2, единицы АЦП
  float temp = NAN;      // последнее показание NTC, °C
  bool  has = false;
  bool  smokeHi = false; // дым выше порога (с гистерезисом — защита от дребезга)
  bool  tempWarnHi = false;  // t выше 45 °C (с гистерезисом)
  bool  tempAlarmHi = false; // t выше 60 °C (с гистерезисом)
  bool  bayPower = true;     // питание отсека: АВАРИЯ размыкает, назад — только «Сброс»
  bool  shownRelay = false;  // что сейчас выставлено на реле
  uint32_t lastReadMs = 0 - FIRE_READ_MS;  // сразу читаем на первом update()

  // Кнопка «Сброс»: нажата — LOW (на GND, INPUT_PULLUP), с подавлением дребезга.
  bool     btnStable = true;   // устойчивое состояние (true — отпущена/HIGH)
  bool     btnLastRaw = true;
  uint32_t btnChangeMs = 0;
};
