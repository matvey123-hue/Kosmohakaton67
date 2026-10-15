#include "2_fire.h"
#include <stdio.h>

void FireSafety::begin() {}

void FireSafety::update(uint32_t nowMs) {}

AlarmLevel FireSafety::level() const { return LVL_OK; }

uint8_t FireSafety::alarmMask() const { return 0; }

const char* FireSafety::message() const { return nullptr; }

void FireSafety::valueText(char* buf, size_t n) const { snprintf(buf, n, "нет данных"); }
