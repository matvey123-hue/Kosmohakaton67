#pragma once
// Заглушка ESP32Servo для тестов: пишет угол в g_servoAngle[pin].
#include "Arduino.h"
#include <unordered_map>

extern std::unordered_map<int, int> g_servoAngle;

class Servo {
public:
  void setPeriodHertz(int) {}
  void attach(int pin, int minUs = 500, int maxUs = 2400) { pin_ = pin; }
  void write(int deg) { g_servoAngle[pin_] = deg; }
private:
  int pin_ = -1;
};
