#pragma once
// Заглушка Adafruit Unified Sensor: только то, что нужно MPU6050.
struct sensors_vec_t {
  float x, y, z;
};
struct sensors_event_t {
  sensors_vec_t acceleration;
};
