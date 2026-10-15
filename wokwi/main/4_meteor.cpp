#include "4_meteor.h"
#include <stdio.h>

void MeteorShield::begin() {}

void MeteorShield::update(uint32_t nowMs) {}

AlarmLevel MeteorShield::level() const { return LVL_OK; }

uint8_t MeteorShield::alarmMask() const { return 0; }

const char* MeteorShield::message() const { return nullptr; }

void MeteorShield::valueText(char* buf, size_t n) const { snprintf(buf, n, "нет данных"); }
