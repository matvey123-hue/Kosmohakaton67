#pragma once
// Заглушки DHT / OneWire / DallasTemperature для тестов №1.
#include <cstdint>

#define DHT22 22

extern float g_dhtT;   // воздух, °C
extern float g_dhtH;   // влажность, %
extern bool g_dhtOk;   // датчик отвечает
extern float g_ds18T;  // обшивка, °C
extern bool g_ds18Ok;  // DS18B20 отвечает

class DHT {
public:
  DHT(int pin, uint8_t type) { (void)pin; (void)type; }
  void begin() {}
  float readTemperature(bool = false, bool = false);
  float readHumidity(bool = false);
};

class OneWire {
public:
  explicit OneWire(int pin) { (void)pin; }
};

typedef uint8_t DeviceAddress[8];

class DallasTemperature {
public:
  explicit DallasTemperature(OneWire*) {}
  void begin() {}
  void setWaitForConversion(bool) {}
  bool getAddress(DeviceAddress addr, uint8_t) {
    if (!g_ds18Ok) return false;
    for (int i = 0; i < 8; i++) addr[i] = (uint8_t)i;
    return true;
  }
  // Читает scratchpad и проверяет CRC — заглушка кладёт туда g_ds18T.
  bool isConnected(DeviceAddress, uint8_t* sp) {
    if (!g_ds18Ok) return false;
    int16_t raw = (int16_t)(g_ds18T * 16.0f);
    sp[0] = (uint8_t)(raw & 0xFF);
    sp[1] = (uint8_t)((raw >> 8) & 0xFF);
    for (int i = 2; i < 9; i++) sp[i] = 0;
    return true;
  }
  void requestTemperatures() {}
};
