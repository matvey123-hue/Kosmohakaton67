#pragma once
// Общее для всех подсистем: уровни тревоги, общие константы и утилиты.
#include <stddef.h>
#include <stdint.h>

// Уровень базы = максимум по подсистемам, поэтому порядок значений важен:
// чем больше число, тем серьёзнее.
enum AlarmLevel : uint8_t {
  LVL_OK    = 0,
  LVL_WARN  = 1,
  LVL_ALARM = 2,
};

// Название уровня для пульта и журнала. Новый уровень — сразу сюда:
// Arduino собирает без предупреждений, пропущенный case компилятор не покажет.
inline const char* levelName(AlarmLevel lvl) {
  switch (lvl) {
    case LVL_OK:    return "НОРМА";
    case LVL_WARN:  return "ВНИМАНИЕ";
    case LVL_ALARM: return "АВАРИЯ";
  }
  return "?";
}

// Что каждая подсистема отдаёт main.ino: через этот интерфейс работают цикл, пульт и журнал.
class Subsystem {
public:
  virtual ~Subsystem() = default;

  virtual const char* name() const = 0;          // «№1 Жизнеобеспечение» — для пульта и журнала
  virtual void        begin() = 0;               // из setup()
  virtual void        update(uint32_t nowMs) = 0; // из loop(): датчики, уровни, исполнители

  virtual AlarmLevel  level() const = 0;          // уровень подсистемы
  virtual uint8_t     alarmMask() const = 0;      // биты параметров в АВАРИИ: новый бит = новая авария
  virtual const char* message() const = 0;        // сообщение для пульта или nullptr
  virtual void        valueText(char* buf, size_t n) const = 0;  // ключевое значение для пульта и журнала
};

// ---- Общие константы (спека, раздел «Общие требования») ----
const uint32_t HOLD_MS = 2000;  // ложные тревоги: новый уровень должен продержаться 2 с
