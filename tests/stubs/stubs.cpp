// Реализация заглушек Arduino и датчиков для юнит-тестов.
#include "Arduino.h"
#include "sensors_stubs.h"
#include <cstdarg>
#include <cmath>

std::unordered_map<int, int> g_analog;
std::unordered_map<int, int> g_digital;
std::unordered_map<int, int> g_written;
std::unordered_map<int, int> g_servoAngle;
uint32_t g_millis = 0;
int32_t g_bmpPressurePa = 101325;
bool g_bmpPresent = true;
float g_accel[3] = {0, 0, 1};
bool g_mpuPresent = true;
float g_dhtT = 22, g_dhtH = 45;
bool g_dhtOk = true;
float g_ds18T = -10;
bool g_ds18Ok = true;
SerialStub Serial;

static std::unordered_map<int, int> g_pinmode;

void pinMode(int pin, int mode) { g_pinmode[pin] = mode; }
void digitalWrite(int pin, int value) { g_written[pin] = value; }
int digitalRead(int pin) {
  // INPUT_PULLUP без внешнего сигнала читается как HIGH.
  auto it = g_digital.find(pin);
  if (it != g_digital.end()) return it->second;
  return g_pinmode[pin] == INPUT_PULLUP ? HIGH : LOW;
}
int analogRead(int pin) {
  auto it = g_analog.find(pin);
  return it != g_analog.end() ? it->second : 0;
}
void tone(int, int, unsigned long) {}
void noTone(int) {}
uint32_t millis() { return g_millis; }
void delay(unsigned long ms) { g_millis += ms; }

void SerialStub::begin(unsigned long) {}
void SerialStub::println(const char*) {}
void SerialStub::print(const char*) {}
void SerialStub::print(unsigned long) {}
int SerialStub::printf(const char*, ...) { return 0; }

float DHT::readTemperature(bool, bool) { return g_dhtOk ? g_dhtT : NAN; }
float DHT::readHumidity(bool) { return g_dhtOk ? g_dhtH : NAN; }
