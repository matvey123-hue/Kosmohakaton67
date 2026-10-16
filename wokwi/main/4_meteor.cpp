#include "4_meteor.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>

void MeteorShield::begin() {
  mpuOk = mpu.begin(cfg.i2cAddr);
  if (mpuOk) {
    // Удары дают до ~±2 g по осям: диапазона ±4 g хватает, запас под модуль есть.
    mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);  // режем шум, удары медленнее
  }
  for (int i = 0; i < METEOR_WEAK_STORE; i++) weakTimes[i] = 0;
  Serial.printf("№4: MPU6050 %s\r\n", mpuOk ? "найден" : "не найден");
}

void MeteorShield::readAccel(uint32_t nowMs) {
  if (!mpuOk) return;
  sensors_event_t a, g, t;
  mpu.getEvent(&a, &g, &t);
  // Библиотека отдаёт м/с² — переводим в g.
  float gx = a.acceleration.x / 9.80665f;
  float gy = a.acceleration.y / 9.80665f;
  float gz = a.acceleration.z / 9.80665f;
  float mod = sqrtf(gx * gx + gy * gy + gz * gz);
  if (!isfinite(mod)) return;
  last = mod;
  has = true;

  // Пауза после эпизода: пока она идёт, новые пики игнорируем.
  if ((int32_t)(cooldownUntilMs - nowMs) > 0) return;

  if (inEpisode) {
    if (mod >= METEOR_WEAK_G) {
      weakStreak++;
      if (mod > METEOR_STRONG_G) strongStreak++;
      if (mod > episodePeak) episodePeak = mod;
    } else {
      // Эпизод кончился (отсчёт ниже порога) — засчитываем удар один раз.
      finishEpisode(nowMs);
    }
  } else if (mod >= METEOR_WEAK_G) {
    inEpisode = true;
    weakStreak = 1;
    strongStreak = mod > METEOR_STRONG_G ? 1 : 0;
    episodePeak = mod;
  }
}

// Пик — серия из METEOR_CONFIRM_READS отсчётов подряд: одиночный выброс
// (один отсчёт) ударом не считается.
void MeteorShield::finishEpisode(uint32_t nowMs) {
  inEpisode = false;
  cooldownUntilMs = nowMs + METEOR_COOLDOWN_MS;  // один удар — один счёт
  if (weakStreak < METEOR_CONFIRM_READS) {
    weakStreak = strongStreak = 0;
    return;
  }
  if (episodePeak > METEOR_STRONG_G && strongStreak >= METEOR_CONFIRM_READS) {
    strongTotal++;
    strongSeq++;
    alarmUntilMs = nowMs + METEOR_ALARM_HOLD_MS;
    strongPending = true;  // main.ino заберёт и передаст в №3
    Serial.printf("№4: СИЛЬНЫЙ удар %.2f g (всего %lu)\r\n",
                  (double)episodePeak, (unsigned long)strongTotal);
  } else {
    weakTotal++;
    weakTimes[weakIdx % METEOR_WEAK_STORE] = nowMs;
    weakIdx++;
    Serial.printf("№4: слабый удар %.2f g (всего %lu)\r\n",
                  (double)episodePeak, (unsigned long)weakTotal);
  }
  weakStreak = strongStreak = 0;
  episodePeak = 0;
}

int MeteorShield::countWeakInWindow(uint32_t nowMs) const {
  int n = 0;
  int stored = weakIdx < METEOR_WEAK_STORE ? weakIdx : METEOR_WEAK_STORE;
  for (int i = 0; i < stored; i++)
    if (nowMs - weakTimes[i] < METEOR_WEAK_WINDOW_MS) n++;
  return n;
}

void MeteorShield::update(uint32_t nowMs) {
  if (nowMs - lastReadMs >= METEOR_READ_MS) {
    lastReadMs = nowMs;
    readAccel(nowMs);
  }
  // ВНИМАНИЕ: 3+ слабых удара за 60 с — с выдержкой 2 с, как у остальных.
  weakInWindow = countWeakInWindow(nowMs);
  warnHold.update(weakInWindow >= METEOR_WEAK_COUNT ? LVL_WARN : LVL_OK, nowMs);
}

AlarmLevel MeteorShield::level() const {
  // alarmUntilMs == 0 — ударов ещё не было: иначе до первого update()
  // сравнение с начальным lastReadMs дало бы ложную АВАРИЮ.
  if (alarmUntilMs != 0 && (int32_t)(alarmUntilMs - lastReadMs) > 0) return LVL_ALARM;
  return warnHold.level;
}

uint8_t MeteorShield::alarmMask() const {
  if (level() != LVL_ALARM) return 0;
  // Бит крутится по счётчику сильных ударов: новый удар — новый бит,
  // и main.ino понимает, что это новая авария, а не продолжение старой.
  return 1 << (strongSeq % 8);
}

const char* MeteorShield::message() const {
  if (level() == LVL_ALARM) return "Сильный удар! Проверить герметичность";
  if (level() == LVL_WARN) return "Серия слабых ударов";
  return nullptr;
}

void MeteorShield::valueText(char* buf, size_t n) const {
  if (!has) {
    snprintf(buf, n, "нет данных");
    return;
  }
  snprintf(buf, n, "ударов %lu (слаб. %lu, сильн. %lu), тек. %.2f g",
           (unsigned long)(weakTotal + strongTotal),
           (unsigned long)weakTotal, (unsigned long)strongTotal, (double)last);
}
