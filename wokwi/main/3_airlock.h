#pragma once
// Подсистема №3 — шлюз и герметичность. Заготовка: интерфейс есть, логики ещё нет.
#include "common.h"

// Подключение железа подсистемы — заполняет main.ino по схеме.
struct AirlockConfig {
};

class Airlock : public Subsystem {
public:
  explicit Airlock(const AirlockConfig& cfg) : cfg(cfg) {}

  const char* name() const override { return "№3 Шлюз и герметичность"; }
  void        begin() override;
  void        update(uint32_t nowMs) override;
  AlarmLevel  level() const override;
  uint8_t     alarmMask() const override;
  const char* message() const override;
  void        valueText(char* buf, size_t n) const override;

private:
  const AirlockConfig cfg;
};
