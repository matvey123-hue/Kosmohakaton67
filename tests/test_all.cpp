// Юнит-тесты логики подсистем: g++ -std=c++17 tests/test_all.cpp и запуск.
// Проверяют пороги, выдержки, гистерезис и автореакции без железа.
#include <cstdio>
#include <cstring>
#include <cmath>
#include "Arduino.h"
#include "sensors_stubs.h"
#include "ESP32Servo.h"
#include "common.h"
#include "1_life_support.h"
#include "2_fire.h"
#include "3_airlock.h"
#include "4_meteor.h"

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

// Прогон update() с шагом stepMs до момента toMs.
template <typename T>
static void runUntil(T& sub, uint32_t& now, uint32_t toMs, uint32_t stepMs = 50) {
  while (now <= toMs) { g_millis = now; sub.update(now); now += stepMs; }
}

static void resetWorld() {
  g_analog.clear(); g_digital.clear(); g_written.clear(); g_servoAngle.clear();
  g_bmpPressurePa = 101325; g_bmpPresent = true;
  g_accel[0] = 0; g_accel[1] = 0; g_accel[2] = 1; g_mpuPresent = true;
  g_dhtT = 22; g_dhtH = 45; g_dhtOk = true;
  g_ds18T = -10; g_ds18Ok = true;
}

// ADC для заданной температуры NTC (обратная формула к ntcToCelsius).
static int ntcAdc(float tC) {
  const float tK = tC + 273.15f;
  const float r = 10000.0f * expf(3950.0f * (1.0f / tK - 1.0f / 298.15f));
  const float v = r / (10000.0f + r);  // NTC сверху, 10 кОм снизу
  return (int)(v * 4095.0f);
}

// ---------- №2 Пожар ----------

static void test_fire_smoke_only_warn() {
  resetWorld();
  FireSafetyConfig cfg{34, 35, 16, true, 17};
  FireSafety f(cfg);
  g_analog[34] = 2500;             // дым выше порога
  g_analog[35] = ntcAdc(24);       // температура в норме
  g_digital[17] = HIGH;            // «Сброс» отпущена
  f.begin();
  uint32_t now = 0;
  runUntil(f, now, 5000);
  CHECK(f.level() == LVL_WARN);    // только один признак — ВНИМАНИЕ
  CHECK(f.bayPowerOn());           // реле не трогаем
  printf("ok fire_smoke_only_warn\n");
}

static void test_fire_both_alarm() {
  resetWorld();
  FireSafetyConfig cfg{34, 35, 16, true, 17};
  FireSafety f(cfg);
  g_analog[34] = 2500;
  g_analog[35] = ntcAdc(50);       // дым + перегрев
  g_digital[17] = HIGH;
  f.begin();
  uint32_t now = 0;
  runUntil(f, now, 1000);
  CHECK(f.level() != LVL_ALARM);   // выдержка: раньше 2 с АВАРИИ нет
  runUntil(f, now, 5000);
  CHECK(f.level() == LVL_ALARM);
  CHECK(!f.bayPowerOn());          // отсек обесточен сразу
  CHECK(g_written[16] == LOW);
  printf("ok fire_both_alarm\n");
}

static void test_fire_short_spike_no_alarm() {
  resetWorld();
  FireSafetyConfig cfg{34, 35, 16, true, 17};
  FireSafety f(cfg);
  g_analog[34] = 1000;
  g_analog[35] = ntcAdc(24);
  g_digital[17] = HIGH;
  f.begin();
  uint32_t now = 0;
  runUntil(f, now, 3000);
  // Всплеск дыма и температуры на 0.5 с — АВАРИИ быть не должно.
  g_analog[34] = 2500;
  g_analog[35] = ntcAdc(55);
  runUntil(f, now, 3500);
  g_analog[34] = 1000;
  g_analog[35] = ntcAdc(24);
  runUntil(f, now, 8000);
  CHECK(f.level() != LVL_ALARM);
  printf("ok fire_short_spike_no_alarm\n");
}

static void test_fire_reset_only_from_ok() {
  resetWorld();
  FireSafetyConfig cfg{34, 35, 16, true, 17};
  FireSafety f(cfg);
  g_analog[34] = 2500;
  g_analog[35] = ntcAdc(55);
  g_digital[17] = HIGH;
  f.begin();
  uint32_t now = 0;
  runUntil(f, now, 5000);
  CHECK(f.level() == LVL_ALARM && !f.bayPowerOn());
  // Жмём «Сброс» прямо во время пожара — питание НЕ возвращается.
  g_digital[17] = LOW;
  runUntil(f, now, 6000);
  CHECK(!f.bayPowerOn());
  // Убираем причину, ждём НОРМУ, жмём снова — питание вернулось.
  g_digital[17] = HIGH;
  g_analog[34] = 1000;
  g_analog[35] = ntcAdc(24);
  runUntil(f, now, 10000);
  CHECK(f.level() == LVL_OK);
  g_digital[17] = LOW;
  runUntil(f, now, 11000);
  CHECK(f.bayPowerOn());
  printf("ok fire_reset_only_from_ok\n");
}

// ---------- №3 Шлюз ----------

static void test_airlock_doors_alarm_and_lock() {
  resetWorld();
  AirlockConfig cfg{19, 39, 13, 500, 2400};
  Airlock a(cfg);
  g_digital[19] = HIGH;  // внешняя закрыта (подтяжка, концевик на GND)
  g_digital[39] = LOW;   // внутренняя закрыта (slide-switch на GND)
  a.begin();
  uint32_t now = 0;
  runUntil(a, now, 2000);
  CHECK(a.level() == LVL_OK);
  CHECK(a.lockDeg() == AIRLOCK_LOCK_OPEN_DEG);
  // Открываем внешнюю — замок закрывается сразу.
  g_digital[19] = LOW;
  runUntil(a, now, 2500);
  CHECK(a.lockDeg() == AIRLOCK_LOCK_CLOSED_DEG);
  CHECK(a.level() == LVL_OK);  // одна дверь — не авария
  // Открываем и внутреннюю — через 2 с АВАРИЯ.
  g_digital[39] = HIGH;
  runUntil(a, now, 3000);
  CHECK(a.level() != LVL_ALARM);
  runUntil(a, now, 6000);
  CHECK(a.level() == LVL_ALARM);
  printf("ok airlock_doors_alarm_and_lock\n");
}

static void test_airlock_pressure_drop_warn() {
  resetWorld();
  AirlockConfig cfg{19, 39, 13, 500, 2400};
  Airlock a(cfg);
  g_digital[19] = HIGH;
  g_digital[39] = LOW;
  a.begin();
  uint32_t now = 0;
  runUntil(a, now, 2000);
  // Резко снижаем давление на 3 кПа, но остаёмся выше 90 — ВНИМАНИЕ по падению.
  g_bmpPressurePa = 98000;
  runUntil(a, now, 6000);
  CHECK(a.level() == LVL_WARN);
  // Ниже 90 кПа — АВАРИЯ.
  g_bmpPressurePa = 89000;
  runUntil(a, now, 10000);
  CHECK(a.level() == LVL_ALARM);
  printf("ok airlock_pressure_drop_warn\n");
}

static void test_airlock_impact_warn_30s() {
  resetWorld();
  AirlockConfig cfg{19, 39, 13, 500, 2400};
  Airlock a(cfg);
  g_digital[19] = HIGH;
  g_digital[39] = LOW;
  a.begin();
  uint32_t now = 0;
  runUntil(a, now, 2000);
  a.notifyStrongImpact(now);
  runUntil(a, now, 5000);
  CHECK(a.level() == LVL_WARN && a.impactWarn());
  runUntil(a, now, 2000 + 31000);
  CHECK(!a.impactWarn() && a.level() == LVL_OK);  // 30 с прошло — само снялось
  printf("ok airlock_impact_warn_30s\n");
}

// ---------- №4 Метеориты ----------

static void setG(float g) {
  // Удар вдоль одной оси: модуль = |g|.
  g_accel[0] = 0; g_accel[1] = 0; g_accel[2] = g;
}

// Один удар: несколько отсчётов выше порога, потом покой 1 g.
static void удар(MeteorShield& m, uint32_t& now, float peak, int reads = 3) {
  for (int i = 0; i < reads; i++) { setG(peak); g_millis = now; m.update(now); now += 100; }
  setG(1.0f);
  for (int i = 0; i < 20; i++) { g_millis = now; m.update(now); now += 100; }
}

static void test_meteor_weak_series_warn() {
  resetWorld();
  MeteorShieldConfig cfg{0x68};
  MeteorShield m(cfg);
  m.begin();
  uint32_t now = 0;
  runUntil(m, now, 1000);
  CHECK(m.level() == LVL_OK);
  удар(m, now, 1.5f);
  удар(m, now, 1.5f);
  CHECK(m.level() != LVL_WARN);  // два удара — ещё не ВНИМАНИЕ
  удар(m, now, 1.5f);
  runUntil(m, now, now + 3000);
  CHECK(m.level() == LVL_WARN);  // три слабых за 60 с
  CHECK(m.totalImpacts() == 3);
  CHECK(!m.takeStrongImpact());
  printf("ok meteor_weak_series_warn\n");
}

static void test_meteor_strong_alarm_and_link() {
  resetWorld();
  MeteorShieldConfig cfg{0x68};
  MeteorShield m(cfg);
  m.begin();
  uint32_t now = 0;
  runUntil(m, now, 1000);
  удар(m, now, 2.5f);
  CHECK(m.level() == LVL_ALARM);
  CHECK(m.strongTotalCount() == 1);
  CHECK(m.takeStrongImpact());   // main.ino забирает флаг для №3
  CHECK(!m.takeStrongImpact());  // флаг одноразовый
  uint8_t mask1 = m.alarmMask();
  // Второй сильный удар — новый бит маски (новая авария для звука).
  удар(m, now, 2.5f);
  CHECK(m.alarmMask() != mask1);
  CHECK(m.takeStrongImpact());
  printf("ok meteor_strong_alarm_and_link\n");
}

static void test_meteor_single_spike_ignored() {
  resetWorld();
  MeteorShieldConfig cfg{0x68};
  MeteorShield m(cfg);
  m.begin();
  uint32_t now = 0;
  runUntil(m, now, 1000);
  // Один отсчёт выше порога — выброс, не удар.
  setG(2.5f);
  g_millis = now; m.update(now); now += 100;
  setG(1.0f);
  runUntil(m, now, now + 5000);
  CHECK(m.totalImpacts() == 0 && m.level() == LVL_OK);
  printf("ok meteor_single_spike_ignored\n");
}

// ---------- №1 Жизнеобеспечение (регрессия: пины и автореакции) ----------

static void test_life_heater_damper() {
  resetWorld();
  LifeSupportConfig cfg{33, DHT22, 2, 12, true, 15, 500, 2400};
  LifeSupport l(cfg);
  g_dhtT = 16; g_dhtH = 45;
  l.begin();
  uint32_t now = 0;
  runUntil(l, now, 6000, 100);
  CHECK(l.heaterOn());            // t < 18 — обогрев включён
  CHECK(g_written[12] == HIGH);   // реле active-high
  g_dhtT = 28;                    // жара — заслонка открывается
  runUntil(l, now, now + 6000, 100);
  CHECK(g_servoAngle[15] == 90);
  CHECK(!l.heaterOn());
  printf("ok life_heater_damper\n");
}

int main() {
  test_fire_smoke_only_warn();
  test_fire_both_alarm();
  test_fire_short_spike_no_alarm();
  test_fire_reset_only_from_ok();
  test_airlock_doors_alarm_and_lock();
  test_airlock_pressure_drop_warn();
  test_airlock_impact_warn_30s();
  test_meteor_weak_series_warn();
  test_meteor_strong_alarm_and_link();
  test_meteor_single_spike_ignored();
  test_life_heater_damper();
  if (failures == 0) printf("ALL TESTS PASSED\n");
  else printf("FAILURES: %d\n", failures);
  return failures ? 1 : 0;
}
