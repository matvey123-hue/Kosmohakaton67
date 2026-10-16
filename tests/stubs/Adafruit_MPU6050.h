#pragma once
// Заглушка Adafruit_MPU6050 для тестов: ускорение задаёт тест через g_accel[3] (g).
#include "Adafruit_Sensor.h"
#include <cstdint>

#define MPU6050_RANGE_2_G 0
#define MPU6050_RANGE_4_G 1
#define MPU6050_BAND_21_HZ 5

extern float g_accel[3];
extern bool g_mpuPresent;

class Adafruit_MPU6050 {
public:
  bool begin(uint8_t addr = 0x68) { (void)addr; return g_mpuPresent; }
  void setAccelerometerRange(int) {}
  void setFilterBandwidth(int) {}
  bool getEvent(sensors_event_t* a, sensors_event_t* g, sensors_event_t* t) {
    if (!g_mpuPresent) return false;
    // Библиотека отдаёт м/с² — заглушка тоже, код переводит в g сам.
    a->acceleration.x = g_accel[0] * 9.80665f;
    a->acceleration.y = g_accel[1] * 9.80665f;
    a->acceleration.z = g_accel[2] * 9.80665f;
    (void)g; (void)t;
    return true;
  }
};
