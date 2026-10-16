#pragma once
// Подсистема №3 — шлюз и герметичность.
// Класс владеет своим железом: читает две двери и BMP180 (давление),
// считает уровень тревоги и сам управляет серво замка внутренней двери.
// Сигнал сильного удара из №4 принимает методом notifyStrongImpact() —
// его вызывает sketch.ino, сами подсистемы друг друга не знают.
// Про схему (diagram.json) он ничего не знает: пины передаёт sketch.ino в AirlockConfig.
#include <Adafruit_BMP085.h>
#include <ESP32Servo.h>
#include "common.h"

// Пороги — в common.h (кейс: «пороги в одном месте»).

// Подключение железа подсистемы — заполняет sketch.ino по схеме.
struct AirlockConfig {
  int pinDoorOuter;  // внешняя дверь: концевик на GND, INPUT_PULLUP
  int pinDoorInner;  // внутренняя дверь: у GPIO 34–39 нет подтяжки,
                     // поэтому slide-switch между 3V3 и GND, обычный INPUT
  int pinLock;       // сигнал серво замка внутренней двери
  int lockMinUs;     // длительность импульса серво для 0°
  int lockMaxUs;     // и для 180°
};

class Airlock : public Subsystem {
public:
  explicit Airlock(const AirlockConfig& cfg) : cfg(cfg) {}

  // ---- Subsystem ----
  const char* name() const override { return "№3 Шлюз и герметичность"; }
  void        begin() override;
  void        update(uint32_t nowMs) override;
  AlarmLevel  level() const override;
  // Бит 0 — давление ниже 90 кПа, бит 1 — обе двери открыты.
  uint8_t     alarmMask() const override;
  const char* message() const override;
  // «давл. 101.3 кПа, внеш. закр, внутр. закр»
  void        valueText(char* buf, size_t n) const override;

  // ---- Связь с №4 (вызывает sketch.ino) ----
  // Сильный удар (> 1,8 g): уровень подсистемы поднимается до ВНИМАНИЯ на 30 с.
  void notifyStrongImpact(uint32_t nowMs) { impactUntilMs = nowMs + AIRLOCK_IMPACT_WARN_MS; }

  // ---- Подробности для пульта ----
  float pressureKpa() const { return pressure; }
  bool  hasPressure() const { return has; }       // было ли хоть одно показание BMP180
  bool  outerOpen() const   { return outer; }     // внешняя дверь открыта
  bool  innerOpen() const   { return inner; }     // внутренняя дверь открыта
  int   lockDeg() const     { return lock; }      // угол серво замка
  bool  impactWarn() const  { return impactOn; }  // идёт 30-секундное ВНИМАНИЕ после удара

private:
  void readDoors(uint32_t nowMs);
  void readPressure(uint32_t nowMs);
  void driveLock();

  const AirlockConfig cfg;
  Adafruit_BMP085 bmp;
  Servo lockServo;

  LevelHold pressHold;  // давление: выдержка 2 с + гистерезис
  LevelHold doorHold;   // обе двери: выдержка 2 с (щелчок переключателем — не авария)

  float pressure = NAN;  // последнее показание BMP180, кПа
  bool  has = false;
  bool  bmpOk = true;
  bool  bmpFound = false;
  uint32_t bmpRetryMs = 0;

  bool outer = false, inner = false;  // двери открыты (после подавления дребезга)
  bool outerRaw = false, innerRaw = false;
  uint32_t outerChangeMs = 0, innerChangeMs = 0;

  // Падение давления: кольцо проб (раз в секунду, окно 10 с).
  // ВНИМАНИЕ — если максимум окна выше текущего больше чем на 2 кПа.
  static const int DROP_N = 11;
  float    dropBuf[DROP_N];
  uint32_t dropWhen[DROP_N];
  int      dropCount = 0;

  uint32_t impactUntilMs = 0;  // до этого момента — ВНИМАНИЕ после сильного удара
  bool     impactOn = false;

  int      lock = AIRLOCK_LOCK_OPEN_DEG;  // решение для серво
  int      shownLock = -1;
  uint32_t lastReadMs = 0 - AIRLOCK_READ_MS;  // сразу читаем на первом update()
};
