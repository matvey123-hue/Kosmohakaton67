#pragma once
// Подсистема №4 — метеоритная защита.
// Класс владеет своим железом: читает MPU6050 (модуль ускорения),
// считает удары и ведёт их счётчик. Сильный удар наружу отдаёт флагом
// takeStrongImpact() — его забирает main.ino и передаёт в №3.
// Про схему (diagram.json) он ничего не знает: адрес MPU передаёт main.ino в конфиге.
#include <Adafruit_MPU6050.h>
#include "common.h"

// Пороги — в common.h (кейс: «пороги в одном месте»).

// Подключение железа подсистемы — заполняет main.ino по схеме.
struct MeteorShieldConfig {
  uint8_t i2cAddr = 0x68;  // адрес MPU6050 (AD0 на GND); шину запускает main.ino
};

class MeteorShield : public Subsystem {
public:
  explicit MeteorShield(const MeteorShieldConfig& cfg) : cfg(cfg) {}

  // ---- Subsystem ----
  const char* name() const override { return "№4 Метеоритная защита"; }
  void        begin() override;
  void        update(uint32_t nowMs) override;
  // АВАРИЯ держится METEOR_ALARM_HOLD_MS после сильного удара (выдержка 2 с
  // здесь не применяется: удар — миг, его подтверждает серия отсчётов, спека раздел 2).
  AlarmLevel  level() const override;
  // Бит АВАРИИ крутится по счётчику сильных ударов: новый удар — новый бит,
  // main.ino видит смену маски и снова включает звук (спека, раздел 8).
  uint8_t     alarmMask() const override;
  const char* message() const override;
  // «ударов 5 (слаб. 4, сильн. 1), тек. 1.02 g»
  void        valueText(char* buf, size_t n) const override;

  // ---- Связь с №3 (читает main.ino) ----
  // true — один раз на каждый сильный удар; вызов сбрасывает флаг.
  bool takeStrongImpact() { bool s = strongPending; strongPending = false; return s; }

  // ---- Подробности для пульта ----
  uint32_t totalImpacts() const  { return weakTotal + strongTotal; }
  uint32_t weakTotalCount() const { return weakTotal; }
  uint32_t strongTotalCount() const { return strongTotal; }
  int      weakLast60s() const   { return weakInWindow; }
  float    lastG() const         { return last; }
  bool     hasData() const       { return has; }

private:
  void readAccel(uint32_t nowMs);
  void finishEpisode(uint32_t nowMs);
  int  countWeakInWindow(uint32_t nowMs) const;

  const MeteorShieldConfig cfg;
  Adafruit_MPU6050 mpu;
  bool mpuOk = false;

  LevelHold warnHold;  // ВНИМАНИЕ по числу слабых ударов — с выдержкой 2 с

  float last = 1.0f;  // последний модуль ускорения, g
  bool  has = false;
  uint32_t lastReadMs = 0 - METEOR_READ_MS;  // сразу читаем на первом update()

  // Эпизод удара: серия отсчётов выше порога. Считаем один раз в конце эпизода.
  int      weakStreak = 0;    // подряд отсчётов с g ≥ METEOR_WEAK_G
  int      strongStreak = 0;  // подряд отсчётов с g > METEOR_STRONG_G
  float    episodePeak = 0;   // максимум g за эпизод
  bool     inEpisode = false;
  uint32_t cooldownUntilMs = 0;  // пауза после эпизода: один удар — один счёт

  uint32_t weakTimes[METEOR_WEAK_STORE];  // метки слабых ударов для окна 60 с
  int      weakIdx = 0;
  uint32_t weakTotal = 0, strongTotal = 0;
  int      weakInWindow = 0;

  uint32_t alarmUntilMs = 0;  // до этого момента — АВАРИЯ после сильного удара
  bool     strongPending = false;  // новый сильный удар, ещё не забрал main.ino
  uint32_t strongSeq = 0;          // счётчик сильных ударов — крутит бит маски
};
