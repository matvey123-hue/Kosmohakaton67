#pragma once
// Подсистема №1 — жизнеобеспечение.
// Модуль не знает про пины и библиотеки датчиков: ему передают уже прочитанные
// значения, а он считает уровень тревоги и решает, что делать обогреву и заслонке.
#include <stdint.h>
#include "alarm_level.h"

// ---- Пороги (по спеке, раздел «Подсистемы») ----
// Воздух, °C: НОРМА 18–26, ВНИМАНИЕ 15–18 или 26–30, АВАРИЯ < 15 или > 30.
const float LS_AIR_ALARM_LO  = 15.0f;
const float LS_AIR_WARN_LO   = 18.0f;
const float LS_AIR_WARN_HI   = 26.0f;
const float LS_AIR_ALARM_HI  = 30.0f;
const float LS_AIR_HYST      = 1.0f;    // возврат на уровень ниже — только с запасом 1 °C
// Влажность, %: НОРМА 30–60, ВНИМАНИЕ 20–30 или 60–80, АВАРИЯ < 20 или > 80.
const float LS_HUM_ALARM_LO  = 20.0f;
const float LS_HUM_WARN_LO   = 30.0f;
const float LS_HUM_WARN_HI   = 60.0f;
const float LS_HUM_ALARM_HI  = 80.0f;
const float LS_HUM_HYST      = 2.0f;
// Обшивка, °C: ВНИМАНИЕ ниже −30, АВАРИИ по обшивке в кейсе нет.
const float LS_HULL_WARN_LO  = -30.0f;
const float LS_HULL_HYST     = 1.0f;

const uint32_t LS_HOLD_MS    = 2000;    // новое значение уровня должно продержаться 2 с

// ---- Автореакции (срабатывают сразу, без выдержки) ----
const float LS_HEATER_ON_BELOW   = 18.0f;  // обогрев вкл при t < 18
const float LS_HEATER_OFF_FROM   = 20.0f;  // выкл при t ≥ 20
const float LS_DAMPER_OPEN_ABOVE = 26.0f;  // заслонка 90° при t > 26
const float LS_DAMPER_CLOSE_TO   = 24.0f;  // 0° при t ≤ 24
const int   LS_DAMPER_OPEN_DEG   = 90;
const int   LS_DAMPER_CLOSED_DEG = 0;

// Параметры подсистемы, у каждого своя цветная метка на пульте.
enum LsParam : uint8_t { LS_AIR, LS_HUM, LS_HULL, LS_PARAM_COUNT };

void lifeInit();

// Новое показание DHT22. Если чтение не удалось (NaN) — не вызывать.
void lifeUpdateAir(float airC, float humidity, uint32_t nowMs);
// Новое показание DS18B20. Если датчик не ответил (−127) — не вызывать.
void lifeUpdateHull(float hullC, uint32_t nowMs);

// ---- Для пульта и журнала ----
AlarmLevel  lifeLevel();                       // уровень подсистемы = максимум по параметрам
AlarmLevel  lifeParamLevel(LsParam p);          // уровень одного параметра (для метки на пульте)
uint8_t     lifeAlarmMask();                    // биты параметров в АВАРИИ: новый бит = новая авария
float       lifeAirC();
float       lifeHumidity();
float       lifeHullC();
bool        lifeHasAir();                       // было ли хоть одно показание DHT22
bool        lifeHasHull();                      // было ли хоть одно показание DS18B20
const char* lifeMessage();                      // сообщение для пульта или nullptr

// ---- Команды исполнителям ----
bool        lifeHeaterOn();                     // реле обогревателя
int         lifeDamperDeg();                    // угол серво заслонки
