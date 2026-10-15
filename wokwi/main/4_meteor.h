#pragma once
// Подсистема №4 — метеоритная защита. Заготовка: интерфейс есть, логики ещё нет.
#include "common.h"

// Подключение железа подсистемы — заполняет main.ino по схеме.
struct MeteorShieldConfig {
};

class MeteorShield : public Subsystem {
public:
  explicit MeteorShield(const MeteorShieldConfig& cfg) : cfg(cfg) {}

  const char* name() const override { return "№4 Метеоритная защита"; }
  void        begin() override;
  void        update(uint32_t nowMs) override;
  AlarmLevel  level() const override;
  uint8_t     alarmMask() const override;
  const char* message() const override;
  void        valueText(char* buf, size_t n) const override;

private:
  const MeteorShieldConfig cfg;
};
