// Мониторинг лунной базы (спека v7), ESP32 в Wokwi. Структура кода — спека, раздел 7.
//
// main.ino — единственное место, которое знает схему: пины и параметры железа
// каждой подсистемы. Создаёт объекты подсистем, вызывает их begin() и update().
// Общее железо: шина I2C, экран, зуммер, кнопка «Подтвердить», RGB-светодиод.
// Общие части: уровень базы, пульт, звук, журнал. Связи подсистем (№4 → №3).
#include <SPI.h>
#include <Wire.h>
#include <math.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "1_life_support.h"
#include "2_fire.h"
#include "3_airlock.h"
#include "4_meteor.h"

// ---- Общее железо (спека, раздел 4) ----
const int PIN_TFT_CS   = 5;   // экран ILI9341
const int PIN_TFT_DC   = 4;
const int PIN_SPI_SCK  = 18;  // MISO экрану не нужен — GPIO19 отдан RGB (см. setup)
const int PIN_SPI_MOSI = 23;
// RST экрана в распиновке не указан: в схеме висит на 3V3, здесь передаём -1.
const int PIN_BUZZER   = 25;  // зуммер (активный)
const int PIN_RGB_R    = 26;  // RGB, общий катод: HIGH = горит
const int PIN_RGB_G    = 27;
const int PIN_RGB_B    = 14;
const int PIN_ACK      = 32;  // кнопка «Подтвердить» на GND, INPUT_PULLUP
const int PIN_I2C_SDA  = 21;  // общая шина: BMP180 (№3) и MPU6050 (№4)
const int PIN_I2C_SCL  = 22;

// ---- Подсистемы: всё, что каждая знает о схеме (спека, раздел 4) ----

const LifeSupportConfig LIFE_CFG = {
  .pinAir           = 33,     // DHT22 DATA, pull-up 4,7 кОм
  .airSensorType    = DHT22,
  .pinHull          = 2,      // DS18B20 DQ, pull-up 4,7 кОм
  .pinHeater        = 12,     // реле обогревателя (в схеме transistor=pnp: HIGH = замкнуто)
  .heaterActiveHigh = true,
  .pinDamper        = 15,     // серво заслонки
  .damperMinUs      = 500,
  .damperMaxUs      = 2400,
};
const FireSafetyConfig FIRE_CFG = {
  .pinSmoke        = 34,      // MQ2 AOUT, ADC1
  .pinTemp         = 35,      // NTC OUT, ADC1
  .pinRelay        = 16,      // реле питания отсека (в схеме transistor=pnp: HIGH = замкнуто)
  .relayActiveHigh = true,
  .pinReset        = 17,      // кнопка «Сброс» на GND, INPUT_PULLUP
};
const AirlockConfig AIRLOCK_CFG = {
  .pinDoorOuter = 19,         // внешняя дверь: концевик на GND, INPUT_PULLUP
  .pinDoorInner = 39,         // внутренняя дверь: slide-switch 3V3/GND, обычный INPUT
  .pinLock      = 13,         // серво замка внутренней двери
  .lockMinUs    = 500,
  .lockMaxUs    = 2400,
};
const MeteorShieldConfig METEOR_CFG = {
  .i2cAddr = 0x68,            // MPU6050 на общей шине I2C
};

LifeSupport  life(LIFE_CFG);
FireSafety   fire(FIRE_CFG);
Airlock      airlock(AIRLOCK_CFG);
MeteorShield meteor(METEOR_CFG);

Adafruit_ILI9341 tft(&SPI, PIN_TFT_DC, PIN_TFT_CS, -1);
U8G2_FOR_ADAFRUIT_GFX u8g;

// Шрифты с кириллицей (U8g2_for_Adafruit_GFX): в них нет «№» и «°»,
// поэтому на пульте пишем «1 ЖИЗНЬ» и «22.0C», а не «№1» и «22.0 °C».
// В журнале (Serial) — полные строки подсистем, там можно всё.

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

// ---- Звук и кнопка «Подтвердить» (спека, раздел 2) ----

const int     SOUND_FREQ_HZ     = 880;    // писк ВНИМАНИЯ и звук АВАРИИ
const uint32_t WARN_BEEP_EVERY_MS = 5000;  // ВНИМАНИЕ: короткий писк раз в 5 с
const uint32_t WARN_BEEP_LEN_MS   = 200;
const uint32_t ACK_DEBOUNCE_MS    = 50;

bool     ackMuted = false;   // «Подтвердить» заглушила звук АВАРИИ
bool     alarmToneOn = false;
uint32_t lastWarnBeepMs = 0;
bool     ackStable = true, ackLastRaw = true;  // кнопка: true — отпущена (HIGH)
uint32_t ackChangeMs = 0;
uint8_t  prevMasks[4] = { 0, 0, 0, 0 };
AlarmLevel prevBase = LVL_OK;

// Новая авария — новые биты в маске любой подсистемы или рост базы до АВАРИИ:
// звук включается снова, даже если «Подтвердить» его глушила.
// Маски обновляем при каждом вызове: иначе одна и та же маска считалась бы
// «новой» вечно и глушение кнопкой сразу же снималось бы.
bool isNewAlarm(const Subsystem* subs[4], AlarmLevel base) {
  bool fresh = base == LVL_ALARM && prevBase != LVL_ALARM;
  for (int i = 0; i < 4; i++) {
    uint8_t cur = subs[i]->alarmMask();
    if ((cur & ~prevMasks[i]) != 0) fresh = true;  // новые биты — новая авария
    prevMasks[i] = cur;
  }
  return fresh;
}

void updateSound(const Subsystem* subs[4], AlarmLevel base, uint32_t nowMs) {
  // Кнопка «Подтвердить»: глушит только звук АВАРИИ, писк ВНИМАНИЯ остаётся.
  // Красная индикация остаётся до устранения причины (ею управляет updateRgb).
  bool raw = digitalRead(PIN_ACK) == HIGH;
  if (raw != ackLastRaw) {
    ackLastRaw = raw;
    ackChangeMs = nowMs;
  }
  if (raw != ackStable && nowMs - ackChangeMs >= ACK_DEBOUNCE_MS) {
    ackStable = raw;
    if (!raw && base == LVL_ALARM && !ackMuted) {
      ackMuted = true;
      Serial.println("Пульт: звук АВАРИИ подтверждён и выключен");
    }
  }

  if (isNewAlarm(subs, base)) {
    if (ackMuted) Serial.println("Пульт: новая авария — звук снова включён");
    ackMuted = false;
  }
  if (base != LVL_ALARM) ackMuted = false;  // причина ушла — глушение снято
  prevBase = base;

  // tone()/noTone() — только при смене звука: каждый вызов кладёт команду
  // в очередь, вызов на каждом проходе loop() её переполнит и остановит цикл.
  if (base == LVL_ALARM && !ackMuted) {
    if (!alarmToneOn) {
      alarmToneOn = true;
      tone(PIN_BUZZER, SOUND_FREQ_HZ);
    }
    lastWarnBeepMs = nowMs;
  } else {
    if (alarmToneOn) {
      alarmToneOn = false;
      noTone(PIN_BUZZER);
    }
    if (base == LVL_WARN && nowMs - lastWarnBeepMs >= WARN_BEEP_EVERY_MS) {
      lastWarnBeepMs = nowMs;
      tone(PIN_BUZZER, SOUND_FREQ_HZ, WARN_BEEP_LEN_MS);
    }
  }
}

// ---- RGB: общий уровень базы (спека, раздел 2 и 4) ----
// Только digitalWrite, без ШИМ: жёлтый = R + G, каналы LEDC остаются серво.
// НОРМА — зелёный, ВНИМАНИЕ — жёлтый, АВАРИЯ — красный мигающий (0,5 с).
// Синий по кейсу не нужен: горит, пока звук заглушён кнопкой («Подтверждено»).
const uint32_t ALARM_BLINK_MS = 500;

void updateRgb(AlarmLevel base, uint32_t nowMs) {
  switch (base) {
    case LVL_OK:
      digitalWrite(PIN_RGB_R, LOW);
      digitalWrite(PIN_RGB_G, HIGH);
      break;
    case LVL_WARN:
      digitalWrite(PIN_RGB_R, HIGH);
      digitalWrite(PIN_RGB_G, HIGH);
      break;
    case LVL_ALARM:
      digitalWrite(PIN_RGB_R, (nowMs / ALARM_BLINK_MS) % 2 == 0 ? HIGH : LOW);
      digitalWrite(PIN_RGB_G, LOW);
      break;
  }
  digitalWrite(PIN_RGB_B, ackMuted ? HIGH : LOW);
}

// ---- Пульт: один экран (спека, раздел 2) ----
// Строка на подсистему (уровень + ключевое значение), общий уровень базы,
// счётчик ударов, состояние реле и заслонок, сообщения. Обновление ≥ 1 раза/с.

const uint16_t COL_BLACK  = 0x0000;
const uint16_t COL_WHITE  = 0xFFFF;
const uint16_t COL_GREEN  = 0x07E0;
const uint16_t COL_YELLOW = 0xFFE0;
const uint16_t COL_RED    = 0xF800;
const uint16_t COL_CYAN   = 0x07FF;
const uint32_t SCREEN_EVERY_MS = 250;  // 4 раза/с: видно мигание АВАРИИ (0,5 с)

uint16_t levelColor(AlarmLevel lvl) {
  switch (lvl) {
    case LVL_OK:    return COL_GREEN;
    case LVL_WARN:  return COL_YELLOW;
    case LVL_ALARM: return COL_RED;
  }
  return COL_WHITE;
}

void drawSubsystemLine(const char* tag, AlarmLevel lvl, const char* text, int y) {
  // Цветная метка уровня, дальше строка подсистемы.
  tft.fillRect(4, y - 11, 12, 12, levelColor(lvl));
  u8g.setForegroundColor(COL_WHITE);
  u8g.setCursor(22, y);
  u8g.print(tag);
  u8g.print(": ");
  u8g.print(text);
}

void updateScreen(AlarmLevel base, uint32_t nowMs) {
  static uint32_t lastDrawMs = 0 - SCREEN_EVERY_MS;
  if (nowMs - lastDrawMs < SCREEN_EVERY_MS) return;
  lastDrawMs = nowMs;

  // Крупная плашка общего уровня. В АВАРИИ мигает (0,5 с) — как RGB.
  bool blinkOff = base == LVL_ALARM && (nowMs / ALARM_BLINK_MS) % 2 != 0;
  tft.fillRect(0, 0, 320, 44, blinkOff ? COL_BLACK : levelColor(base));
  u8g.setFont(u8g2_font_10x20_t_cyrillic);
  u8g.setForegroundColor(blinkOff ? levelColor(base) : COL_BLACK);
  u8g.setCursor(8, 30);
  u8g.print("БАЗА: ");
  u8g.print(levelName(base));

  u8g.setFont(u8g2_font_6x12_t_cyrillic);

  char line[64];
  // Строка подсистемы: уровень + ключевое значение (свои короткие форматы:
  // valueText() идёт в журнал, а на экране нет «№» и «°»).
  if (life.hasAir())
    snprintf(line, sizeof line, "%.1fC вл %.0f%%", (double)life.airC(), (double)life.humidity());
  else if (life.hasHull())
    snprintf(line, sizeof line, "обш %.1fC", (double)life.hullC());
  else
    snprintf(line, sizeof line, "нет данных");
  drawSubsystemLine("1 ЖИЗНЬ", life.level(), line, 62);

  if (fire.hasData()) {
    if (isnan(fire.tempC())) snprintf(line, sizeof line, "дым %d", fire.smokeAdc());
    else snprintf(line, sizeof line, "дым %d t %.1fC", fire.smokeAdc(), (double)fire.tempC());
  } else snprintf(line, sizeof line, "нет данных");
  drawSubsystemLine("2 ПОЖАР", fire.level(), line, 80);

  if (airlock.hasPressure())
    snprintf(line, sizeof line, "%.1f кПа дв %c/%c",
             (double)airlock.pressureKpa(),
             airlock.outerOpen() ? 'O' : 'З', airlock.innerOpen() ? 'O' : 'З');
  else
    snprintf(line, sizeof line, "дв %c/%c",
             airlock.outerOpen() ? 'O' : 'З', airlock.innerOpen() ? 'O' : 'З');
  drawSubsystemLine("3 ШЛЮЗ", airlock.level(), line, 98);

  if (meteor.hasData())
    snprintf(line, sizeof line, "удары %lu тек %.2fg",
             (unsigned long)meteor.totalImpacts(), (double)meteor.lastG());
  else
    snprintf(line, sizeof line, "нет данных");
  drawSubsystemLine("4 МЕТЕОР", meteor.level(), line, 116);

  // Состояние реле и заслонок.
  snprintf(line, sizeof line, "ОБОГР %s ЗАСЛ %d ЗАМОК %s ОТСЕК %s",
           life.heaterOn() ? "ВКЛ" : "выкл", life.damperDeg(),
           airlock.lockDeg() == AIRLOCK_LOCK_CLOSED_DEG ? "ЗАКР" : "откр",
           fire.bayPowerOn() ? "ВКЛ" : "ВЫКЛ");
  u8g.setForegroundColor(COL_CYAN);
  u8g.setCursor(4, 136);
  u8g.print(line);

  // Сообщения подсистем — до трёх строк.
  tft.fillRect(0, 142, 320, 98, COL_BLACK);
  int y = 160;
  const Subsystem* subs[4] = { &life, &fire, &airlock, &meteor };
  for (int i = 0; i < 4 && y <= 200; i++) {
    const char* msg = subs[i]->message();
    if (!msg) continue;
    u8g.setForegroundColor(levelColor(subs[i]->level()));
    u8g.setCursor(4, y);
    u8g.print(msg);
    y += 18;
  }
  // Низ экрана: счётчик ударов и состояние звука.
  u8g.setForegroundColor(COL_WHITE);
  u8g.setCursor(4, 232);
  u8g.print("Удары: ");
  u8g.print((unsigned long)meteor.totalImpacts());
  if (ackMuted) {
    u8g.print("   ЗВУК ВЫКЛ");
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);  // до подсистем: ими пользуются №3 и №4

  // Сначала подсистемы: их серво занимают каналы LEDC. tone() для зуммера
  // берёт свободный канал — запущенный раньше, он отдал бы серво свою частоту.
  life.begin();
  fire.begin();
  airlock.begin();
  meteor.begin();

  // Экран — без MISO: иначе tft.begin() сам запустит SPI с MISO на 19 и заберёт пин у RGB.
  // Повторный SPI.begin() внутри tft.begin() уже ничего не меняет.
  SPI.begin(PIN_SPI_SCK, -1, PIN_SPI_MOSI);
  tft.begin();
  tft.setRotation(1);  // 320×240
  tft.fillScreen(COL_BLACK);
  u8g.begin(tft);
  u8g.setFontMode(1);  // прозрачный фон шрифта

  // RGB — только digitalWrite, без ШИМ: жёлтый = R + G, каналы LEDC остаются серво.
  pinMode(PIN_RGB_R, OUTPUT);
  pinMode(PIN_RGB_G, OUTPUT);
  pinMode(PIN_RGB_B, OUTPUT);
  digitalWrite(PIN_RGB_R, LOW);
  digitalWrite(PIN_RGB_G, LOW);
  digitalWrite(PIN_RGB_B, LOW);
  // Зуммер и кнопка «Подтвердить». tone()/noTone() — только при смене звука
  // (см. updateSound): каждый вызов кладёт команду в очередь, вызов на каждом
  // проходе loop() её переполнит и остановит цикл.
  pinMode(PIN_ACK, INPUT_PULLUP);
  ackStable = digitalRead(PIN_ACK) == HIGH;
  ackLastRaw = ackStable;

  Serial.println("Журнал: мм:сс | подсистема | уровень | значение");
}

void loop() {
  uint32_t now = millis();
  life.update(now);
  fire.update(now);
  airlock.update(now);
  meteor.update(now);

  // Связь подсистем — через main.ino (спека, раздел 7): сильный удар из №4
  // поднимает №3 до ВНИМАНИЯ на 30 с. Флаг одноразовый: take забирает его.
  if (meteor.takeStrongImpact()) airlock.notifyStrongImpact(now);

  const Subsystem* subs[4] = { &life, &fire, &airlock, &meteor };
  AlarmLevel base = LVL_OK;
  for (int i = 0; i < 4; i++)
    if (subs[i]->level() > base) base = subs[i]->level();

  updateSound(subs, base, now);
  updateRgb(base, now);
  updateScreen(base, now);

  static AlarmLevel lifeLogged = LVL_OK, fireLogged = LVL_OK,
                    airlockLogged = LVL_OK, meteorLogged = LVL_OK;
  logLevelChange(life, lifeLogged, now);
  logLevelChange(fire, fireLogged, now);
  logLevelChange(airlock, airlockLogged, now);
  logLevelChange(meteor, meteorLogged, now);
}
