// Мониторинг лунной базы (спека v5), ESP32 в Wokwi. Структура кода — спека, раздел 7.
#include <SPI.h>
#include <Wire.h>
#include "1_life_support.h"
#include "2_fire.h"
#include "3_airlock.h"
#include "4_meteor.h"

// ---- Общее железо (спека, раздел 4) ----
const int PIN_SPI_SCK  = 18;  // экран ILI9341: CS 5, DC 16, RST 17
const int PIN_SPI_MOSI = 23;  // MISO экрану не нужен — GPIO19 отдан RGB
const int PIN_RGB_R    = 2;   // RGB, общий катод: HIGH = горит
const int PIN_RGB_G    = 19;
const int PIN_I2C_SDA  = 21;  // общая шина: BMP180 (№3) и MPU6050 (№4)
const int PIN_I2C_SCL  = 22;

// ---- Подсистемы: всё, что каждая знает о схеме ----

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
const FireSafetyConfig   FIRE_CFG    = {};  // MQ2 34, NTC 35, реле отсека 15, «Сброс» 4
const AirlockConfig      AIRLOCK_CFG = {};  // двери 36 / 39, серво замка 33, BMP180 на I2C
const MeteorShieldConfig METEOR_CFG  = {};  // MPU6050 на I2C

LifeSupport  life(LIFE_CFG);
FireSafety   fire(FIRE_CFG);
Airlock      airlock(AIRLOCK_CFG);
MeteorShield meteor(METEOR_CFG);

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
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);  // до подсистем: ими пользуются №3 и №4

  // Сначала подсистемы: их серво занимают каналы LEDC. tone() для зуммера и analogWrite()
  // берут свободный канал — запущенные раньше, они отдали бы серво свою частоту.
  life.begin();
  fire.begin();
  airlock.begin();
  meteor.begin();

  // Экран — без MISO: иначе tft.begin() сам запустит SPI с MISO на 19 и заберёт пин у RGB.
  // Повторный SPI.begin() внутри tft.begin() уже ничего не меняет.
  SPI.begin(PIN_SPI_SCK, -1, PIN_SPI_MOSI);
  // RGB — только digitalWrite, без ШИМ: жёлтый = R + G, каналы LEDC остаются серво.
  pinMode(PIN_RGB_R, OUTPUT);
  pinMode(PIN_RGB_G, OUTPUT);
  // Зуммер: tone()/noTone() — только при смене звука. Каждый вызов кладёт команду в очередь,
  // вызов на каждом проходе loop() её переполнит и остановит цикл.

  Serial.println("Журнал: мм:сс | подсистема | уровень | значение");
}

void loop() {
  uint32_t now = millis();
  life.update(now);
  fire.update(now);
  airlock.update(now);
  meteor.update(now);

  static AlarmLevel lifeLogged = LVL_OK, fireLogged = LVL_OK, airlockLogged = LVL_OK, meteorLogged = LVL_OK;
  logLevelChange(life, lifeLogged, now);
  logLevelChange(fire, fireLogged, now);
  logLevelChange(airlock, airlockLogged, now);
  logLevelChange(meteor, meteorLogged, now);
}
