#pragma once
// Подсистема №2 — пожарная безопасность. Заготовка: интерфейс есть, логики ещё нет.
#include "common.h"

// Подключение железа подсистемы — заполняет main.ino по схеме.
struct FireSafetyConfig {
};

class FireSafety : public Subsystem {
public:
  explicit FireSafety(const FireSafetyConfig& cfg) : cfg(cfg) {}

  const char* name() const override { return "№2 Пожарная безопасность"; }
  void        begin() override;
  void        update(uint32_t nowMs) override;
  AlarmLevel  level() const override;
  uint8_t     alarmMask() const override;
  const char* message() const override;
  void        valueText(char* buf, size_t n) const override;

private:
  const FireSafetyConfig cfg;
};
