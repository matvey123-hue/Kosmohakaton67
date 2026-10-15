// Демо подсистемы №1 «Жизнеобеспечение» для Wokwi (спека v3).
// Как менять показания: во время симуляции кликнуть по DHT22 (температура и влажность воздуха)
// или по DS18B20 (обшивка) и двигать ползунки.
// Логика — в life_support.h/.cpp (build.bat копирует их из firmware/), здесь железо, экран и журнал.
#include <SPI.h>
#include <Adafruit_ILI9341.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP32Servo.h>
#include "life_support.h"

// ---- Пины по распиновке спеки v3 ----
const int PIN_TFT_CS  = 5;
const int PIN_TFT_DC  = 16;
const int PIN_TFT_RST = 17;   // SCK 18, MISO 19, MOSI 23 — аппаратный SPI
const int PIN_DHT     = 27;   // DHT22 DATA, pull-up 4,7 кОм
const int PIN_DS18    = 26;   // DS18B20 DQ, pull-up 4,7 кОм
const int PIN_HEATER  = 25;   // реле обогревателя, HIGH = включено
const int PIN_DAMPER  = 32;   // серво заслонки
const int PIN_BUZZER  = 13;
const int PIN_BTN_ACK = 14;   // «Подтвердить», на GND

const uint32_t DHT_READ_MS  = 2000;  // DHT22 чаще раза в 2 с не читают
const uint32_t DS18_READ_MS = 1000;  // DS18B20: запрос, через секунду забираем результат
const uint32_t DRAW_MS      = 250;   // пульт обновляется 4 раза в секунду
const uint32_t BLINK_MS     = 500;   // АВАРИЯ мигает с периодом 0,5 с

Adafruit_ILI9341 tft(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2;
DHT dht(PIN_DHT, DHT22);
OneWire oneWire(PIN_DS18);
DallasTemperature ds18(&oneWire);
Servo damper;

uint32_t lastDhtMs, lastDs18Ms, lastDrawMs;
bool     ds18Requested;
bool     prevAck = HIGH;
bool     alarmMuted;        // звук текущей АВАРИИ заглушён кнопкой «Подтвердить»
uint8_t  mutedAlarmMask;    // какие параметры были в АВАРИИ в момент подтверждения

const char* levelName(AlarmLevel lvl) {
  return lvl == LVL_ALARM ? "АВАРИЯ" : lvl == LVL_WARN ? "ВНИМАНИЕ" : "НОРМА";
}

// ---- Датчики ----

void readDht(uint32_t now) {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) {
    Serial.println("DHT22: нет ответа");
    return;
  }
  lifeUpdateAir(t, h, now);
}

// Без ожидания: requestTemperatures() не блокирует, результат забираем на следующем шаге.
void readDs18(uint32_t now) {
  if (ds18Requested) {
    float t = ds18.getTempCByIndex(0);
    if (t == DEVICE_DISCONNECTED_C) Serial.println("DS18B20: нет ответа");
    else lifeUpdateHull(t, now);
  }
  ds18.requestTemperatures();
  ds18Requested = true;
}

// ---- Исполнители: реле обогрева и серво заслонки ----

void driveActuators() {
  static bool heater = false;
  static int  angle = -1;
  if (lifeHeaterOn() != heater) {
    heater = lifeHeaterOn();
    digitalWrite(PIN_HEATER, heater ? HIGH : LOW);
    Serial.printf("Обогрев: %s (воздух %.1f °C)\n", heater ? "ВКЛ" : "выкл", lifeAirC());
  }
  if (lifeDamperDeg() != angle) {
    bool first = angle < 0;
    angle = lifeDamperDeg();
    damper.write(angle);
    if (!first) Serial.printf("Заслонка: %d° (воздух %.1f °C)\n", angle, lifeAirC());
  }
}

// ---- Ввод и звук: ВНИМАНИЕ — писк раз в 5 с; АВАРИЯ — непрерывно, пока не нажали «Подтвердить» ----

void pollAck() {
  bool a = digitalRead(PIN_BTN_ACK);
  if (prevAck == HIGH && a == LOW && lifeLevel() == LVL_ALARM) {
    alarmMuted = true;
    mutedAlarmMask = lifeAlarmMask();
    Serial.println("Подтверждено: звук аварии выключен");
  }
  prevAck = a;
}

// tone()/noTone() в ядре ESP32 3.x шлют сообщение задаче звука — вызываем только при смене частоты.
void setBuzzer(unsigned int freq) {
  static unsigned int current = 0;
  if (freq == current) return;
  if (freq) tone(PIN_BUZZER, freq); else noTone(PIN_BUZZER);
  current = freq;
}

void driveSound(AlarmLevel lvl, uint32_t now) {
  if (lvl != LVL_ALARM) alarmMuted = false;                                  // авария ушла — следующая снова со звуком
  if (alarmMuted && (lifeAlarmMask() & ~mutedAlarmMask)) alarmMuted = false; // в аварию ушёл ещё один параметр
  switch (lvl) {
    case LVL_OK:    setBuzzer(0); break;
    case LVL_WARN:  setBuzzer(now % 5000 < 100 ? 1500 : 0); break;
    case LVL_ALARM: setBuzzer(alarmMuted ? 0 : 2500); break;
  }
}

// ---- Журнал: строка на каждую смену уровня «мм:сс | подсистема | уровень | значение» ----

void logChanges(uint32_t now) {
  static AlarmLevel prevLvl = LVL_OK;
  AlarmLevel lvl = lifeLevel();
  if (lvl == prevLvl) return;
  prevLvl = lvl;
  uint32_t s = now / 1000;
  Serial.printf("%02lu:%02lu | №1 Жизнеобеспечение | %s | воздух %.1f °C, влажность %.0f %%, обшивка %.1f °C\n",
                (unsigned long)(s / 60), (unsigned long)(s % 60), levelName(lvl),
                lifeAirC(), lifeHumidity(), lifeHullC());
}

// ---- Экран: цвет уровня на плашке и метки у параметров (RGB-светодиода нет) ----

uint16_t levelColor(AlarmLevel lvl, uint32_t now) {
  if (lvl == LVL_ALARM) return (now / BLINK_MS) % 2 ? ILI9341_RED : ILI9341_MAROON;
  return lvl == LVL_WARN ? ILI9341_YELLOW : ILI9341_GREEN;
}

// Строка значения: стираем старое и пишем новое (без мерцания всего экрана).
// В кириллическом шрифте нет знака «°», поэтому рисуем его кружком.
void drawValue(int y, const char* text, uint16_t color = ILI9341_WHITE) {
  tft.fillRect(170, y - 14, 150, 18, ILI9341_BLACK);
  u8g2.setForegroundColor(color);
  u8g2.setCursor(170, y);
  const char* deg = strstr(text, "°");
  if (!deg) {
    u8g2.print(text);
    return;
  }
  char head[32];
  snprintf(head, sizeof head, "%.*s", (int)(deg - text), text);
  u8g2.print(head);
  int x = u8g2.getCursorX();
  tft.drawCircle(x + 3, y - 10, 2, color);
  u8g2.setCursor(x + 7, y);
  u8g2.print(deg + strlen("°"));
}

// Цветная метка уровня параметра слева от подписи.
void drawMark(int y, AlarmLevel lvl, uint32_t now) {
  tft.fillRect(8, y - 12, 10, 12, levelColor(lvl, now));
}

void drawStatic() {
  tft.fillScreen(ILI9341_BLACK);
  u8g2.setFont(u8g2_font_10x20_t_cyrillic);
  u8g2.setForegroundColor(ILI9341_CYAN);
  u8g2.setCursor(8, 22);
  u8g2.print("1. Жизнеобеспечение");

  u8g2.setFont(u8g2_font_unifont_t_cyrillic);
  u8g2.setForegroundColor(ILI9341_LIGHTGREY);
  const char* labels[] = {"Воздух:", "Влажность:", "Обшивка:", "Обогрев:", "Заслонка:"};
  for (int i = 0; i < 5; i++) {
    u8g2.setCursor(24, 100 + i * 22);
    u8g2.print(labels[i]);
  }
  u8g2.setForegroundColor(ILI9341_DARKGREY);
  u8g2.setCursor(8, 234);
  u8g2.print("Клик по датчику: ползунки");
}

void drawDynamic(uint32_t now) {
  AlarmLevel lvl = lifeLevel();
  uint16_t color = levelColor(lvl, now);
  char buf[32];

  // Плашка уровня; при заглушённой аварии — пометка.
  tft.fillRect(8, 36, 304, 34, color);
  u8g2.setFont(u8g2_font_10x20_t_cyrillic);
  u8g2.setForegroundColor(lvl == LVL_ALARM ? ILI9341_WHITE : ILI9341_BLACK);
  u8g2.setCursor(16, 60);
  u8g2.print(levelName(lvl));
  if (lvl == LVL_ALARM && alarmMuted) {
    u8g2.setFont(u8g2_font_unifont_t_cyrillic);
    u8g2.setCursor(200, 58);
    u8g2.print("звук выкл");
  }

  u8g2.setFont(u8g2_font_unifont_t_cyrillic);
  drawMark(100, lifeParamLevel(LS_AIR), now);
  drawMark(122, lifeParamLevel(LS_HUM), now);
  drawMark(144, lifeParamLevel(LS_HULL), now);
  if (lifeHasAir()) {
    snprintf(buf, sizeof buf, "%.1f °C", lifeAirC());
    drawValue(100, buf);
    snprintf(buf, sizeof buf, "%.0f %%", lifeHumidity());
    drawValue(122, buf);
  } else {
    drawValue(100, "нет данных", ILI9341_DARKGREY);
    drawValue(122, "нет данных", ILI9341_DARKGREY);
  }
  if (lifeHasHull()) {
    snprintf(buf, sizeof buf, "%.1f °C", lifeHullC());
    drawValue(144, buf);
  } else {
    drawValue(144, "нет данных", ILI9341_DARKGREY);
  }
  drawValue(166, lifeHeaterOn() ? "ВКЛ" : "выкл", lifeHeaterOn() ? ILI9341_ORANGE : ILI9341_WHITE);
  snprintf(buf, sizeof buf, "%d° (%s)", lifeDamperDeg(), lifeDamperDeg() ? "открыта" : "закрыта");
  drawValue(188, buf, lifeDamperDeg() ? ILI9341_CYAN : ILI9341_WHITE);

  // Сообщение пульта: перерисовываем только при смене, иначе строка мерцает.
  static const char* shownMsg = "";
  static AlarmLevel  shownLvl = LVL_OK;
  const char* msg = lifeMessage();
  if (msg == shownMsg && lvl == shownLvl) return;
  shownMsg = msg;
  shownLvl = lvl;
  tft.fillRect(0, 196, 320, 20, ILI9341_BLACK);
  if (msg) {
    u8g2.setForegroundColor(lvl == LVL_ALARM ? ILI9341_RED : ILI9341_YELLOW);
    u8g2.setCursor(8, 212);
    u8g2.print(msg);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BTN_ACK, INPUT_PULLUP);
  pinMode(PIN_HEATER, OUTPUT);
  digitalWrite(PIN_HEATER, LOW);

  tft.begin();
  tft.setRotation(1);  // 320×240, альбомная
  u8g2.begin(tft);
  u8g2.setFontMode(1);

  dht.begin();
  ds18.begin();
  ds18.setWaitForConversion(false);
  Serial.printf("DS18B20 найдено: %d\n", ds18.getDeviceCount());

  damper.setPeriodHertz(50);
  damper.attach(PIN_DAMPER, 500, 2400);

  lifeInit();
  drawStatic();
  Serial.println("Демо №1 запущено. Журнал: мм:сс | подсистема | уровень | значение");
}

void loop() {
  uint32_t now = millis();
  pollAck();
  if (now - lastDhtMs >= DHT_READ_MS) {
    lastDhtMs = now;
    readDht(now);
  }
  if (now - lastDs18Ms >= DS18_READ_MS) {
    lastDs18Ms = now;
    readDs18(now);
  }
  driveActuators();
  driveSound(lifeLevel(), now);
  logChanges(now);
  if (now - lastDrawMs >= DRAW_MS) {
    lastDrawMs = now;
    drawDynamic(now);
  }
}
