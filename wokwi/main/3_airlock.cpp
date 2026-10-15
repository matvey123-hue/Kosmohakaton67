#include "3_airlock.h"
#include <stdio.h>

void Airlock::begin() {}

void Airlock::update(uint32_t nowMs) {}

AlarmLevel Airlock::level() const { return LVL_OK; }

uint8_t Airlock::alarmMask() const { return 0; }

const char* Airlock::message() const { return nullptr; }

void Airlock::valueText(char* buf, size_t n) const { snprintf(buf, n, "нет данных"); }
