#pragma once
// Заглушка Adafruit_BMP085 для тестов: давление задаёт тест через g_bmpPressurePa.
#include <cstdint>

extern int32_t g_bmpPressurePa;  // что «видит» датчик, Па
extern bool g_bmpPresent;        // датчик отвечает на begin()

class Adafruit_BMP085 {
public:
  bool begin() { return g_bmpPresent; }
  int32_t readPressure() { return g_bmpPressurePa; }
  float readTemperature() { return 22.0f; }
};
