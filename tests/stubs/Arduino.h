#pragma once
// Заглушка Arduino.h для юнит-тестов на компьютере (g++).
// Реальное железо заменено глобальными таблицами: тест кладёт туда
// показания датчиков, а код подсистем читает их как обычно.
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <vector>

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

extern std::unordered_map<int, int> g_analog;   // pin -> единицы АЦП
extern std::unordered_map<int, int> g_digital;  // pin -> HIGH/LOW (входы)
extern std::unordered_map<int, int> g_written;  // pin -> последнее digitalWrite
extern uint32_t g_millis;

void pinMode(int pin, int mode);
void digitalWrite(int pin, int value);
int digitalRead(int pin);
int analogRead(int pin);
void tone(int pin, int freq, unsigned long dur = 0);
void noTone(int pin);
uint32_t millis();
void delay(unsigned long ms);

struct SerialStub {
  void begin(unsigned long baud);
  void println(const char* s);
  void print(const char* s);
  void print(unsigned long v);
  int printf(const char* fmt, ...);
};
extern SerialStub Serial;
