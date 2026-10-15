// Мониторинг лунной базы (спека v4), ESP32 в Wokwi. Структура кода — спека, раздел 7.
#include "1_life_support.h"
#include "2_fire.h"
#include "3_airlock.h"
#include "4_meteor.h"

// №1 Жизнеобеспечение — всё, что подсистема знает о схеме.
const LifeSupportConfig LIFE_CFG = {
  .pinAir           = 27,     // DHT22 DATA, pull-up 4,7 кОм
  .airSensorType    = DHT22,
  .pinHull          = 26,     // DS18B20 DQ, pull-up 4,7 кОм
  .pinHeater        = 25,     // реле обогревателя
  .heaterActiveHigh = true,
  .pinDamper        = 32,     // серво заслонки
  .damperMinUs      = 500,
  .damperMaxUs      = 2400,
};

LifeSupport life(LIFE_CFG);

// ---- Журнал (спека, раздел 2): Serial, строка на каждую смену уровня ----

// «мм:сс | подсистема | уровень | значение». prev — последний записанный уровень подсистемы.
void logLevelChange(const Subsystem& sub, AlarmLevel& prev, uint32_t nowMs) {
  AlarmLevel lvl = sub.level();
  if (lvl == prev) return;
  prev = lvl;
  char value[96];
  sub.valueText(value, sizeof value);
  uint32_t s = nowMs / 1000;
  Serial.printf("%02lu:%02lu | %s | %s | %s\r\n",
                (unsigned long)(s / 60), (unsigned long)(s % 60), sub.name(), levelName(lvl), value);
}

void setup() {
  Serial.begin(115200);
  life.begin();
  Serial.println("Журнал: мм:сс | подсистема | уровень | значение");
}

void loop() {
  uint32_t now = millis();
  life.update(now);

  static AlarmLevel lifeLogged = LVL_OK;
  logLevelChange(life, lifeLogged, now);
}
