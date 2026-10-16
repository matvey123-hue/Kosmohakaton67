#pragma once
// Общее для всех подсистем: все пороги, уровни тревоги, интерфейс подсистемы, утилиты.
#include <math.h>
#include <stddef.h>
#include <stdint.h>

// ======== ПОРОГИ — все в одном месте (кейс, требование 7; спека, раздел 3) ========
// Изменили порог — обоснуйте в отчёте.

// ---- Общие ----
const uint32_t HOLD_MS = 2000;  // ложные тревоги: новый уровень должен продержаться 2 с

// ---- №1 Жизнеобеспечение ----
// Воздух, °C: НОРМА 18–26, ВНИМАНИЕ 15–18 или 26–30, АВАРИЯ < 15 или > 30.
const float LS_AIR_ALARM_LO  = 15.0f;
const float LS_AIR_WARN_LO   = 18.0f;
const float LS_AIR_WARN_HI   = 26.0f;
const float LS_AIR_ALARM_HI  = 30.0f;
const float LS_AIR_HYST      = 1.0f;    // возврат на уровень ниже — только с запасом 1 °C
// Влажность, %: НОРМА 30–60, ВНИМАНИЕ 20–30 или 60–80, АВАРИЯ < 20 или > 80.
const float LS_HUM_ALARM_LO  = 20.0f;
const float LS_HUM_WARN_LO   = 30.0f;
const float LS_HUM_WARN_HI   = 60.0f;
const float LS_HUM_ALARM_HI  = 80.0f;
const float LS_HUM_HYST      = 2.0f;
// Обшивка, °C: ВНИМАНИЕ ниже −30, АВАРИИ по обшивке в кейсе нет.
const float LS_HULL_WARN_LO  = -30.0f;
const float LS_HULL_HYST     = 1.0f;
// Автореакции — сразу, без выдержки.
const float LS_HEATER_ON_BELOW   = 18.0f;  // обогрев вкл при t < 18
const float LS_HEATER_OFF_FROM   = 20.0f;  // выкл при t ≥ 20
const float LS_DAMPER_OPEN_ABOVE = 26.0f;  // заслонка 90° при t > 26
const float LS_DAMPER_CLOSE_TO   = 24.0f;  // 0° при t ≤ 24
const int   LS_DAMPER_OPEN_DEG   = 90;
const int   LS_DAMPER_CLOSED_DEG = 0;

// ---- №2 Пожарная безопасность ----
// ВНИМАНИЕ: только один признак — дым выше порога ИЛИ t > 45 °C.
// АВАРИЯ: оба признака дольше 2 с, или t > 60 °C. Всплеск короче 1 с АВАРИЮ не даёт.
const int   FIRE_SMOKE_ADC    = 2000;   // порог дыма по АЦП MQ2 (0–4095) — открытый вопрос, спека раздел 8
const int   FIRE_SMOKE_HYST_ADC = 150;  // гистерезис дыма: назад — только ниже порога на это
const float FIRE_TEMP_WARN_C  = 45.0f;
const float FIRE_TEMP_ALARM_C = 60.0f;
const float FIRE_TEMP_HYST_C  = 2.0f;   // гистерезис температуры, °C
const uint32_t FIRE_READ_MS  = 200;     // опрос MQ2 и NTC чаще не нужно: оба медленные
const uint32_t FIRE_BUTTON_DEBOUNCE_MS = 50;  // дребезг кнопки «Сброс»
// NTC в Wokwi: делитель 10 кОм + 10 кОм NTC (бета 3950), формула из доков Wokwi,
// но под АЦП ESP32 (0–4095, а не 0–1023).
const float FIRE_NTC_BETA_C   = 3950.0f;
const float FIRE_NTC_RNOM_OHM = 10000.0f;  // сопротивление NTC при +25 °C
const int   FIRE_ADC_MAX      = 4095;
// Автореакция: при АВАРИИ реле питания отсека размыкается, обратно — кнопкой «Сброс» после НОРМЫ.

// ---- №3 Шлюз и герметичность ----
// НОРМА: давление ≥ 95 кПа и открыто не больше одной двери.
// ВНИМАНИЕ: 90–95 кПа, или падение больше 2 кПа за 10 с, или сильный удар в №4 (на 30 с).
// АВАРИЯ: давление < 90 кПа, или обе двери открыты.
const float    AIRLOCK_WARN_KPA       = 95.0f;
const float    AIRLOCK_ALARM_KPA      = 90.0f;
const float    AIRLOCK_PRESS_HYST_KPA = 1.0f;  // гистерезис давления при возврате
const float    AIRLOCK_DROP_KPA       = 2.0f;
const uint32_t AIRLOCK_DROP_WINDOW_MS = 10000;
const uint32_t AIRLOCK_IMPACT_WARN_MS = 30000;  // ВНИМАНИЕ после сильного удара в №4
const uint32_t AIRLOCK_READ_MS        = 1000;   // опрос BMP180 и дверей чаще не нужно
const uint32_t AIRLOCK_DOOR_DEBOUNCE_MS = 50;   // дребезг концевиков дверей
// Автореакция: внешняя дверь открыта — замок внутренней закрыт.
const int      AIRLOCK_LOCK_CLOSED_DEG = 0;
const int      AIRLOCK_LOCK_OPEN_DEG   = 90;

// ---- №4 Метеоритная защита ----
// ВНИМАНИЕ: 3 и более слабых ударов (1,3–1,8 g) за 60 с. АВАРИЯ: хотя бы один удар > 1,8 g.
const float    METEOR_WEAK_G          = 1.3f;
const float    METEOR_STRONG_G        = 1.8f;
const int      METEOR_WEAK_COUNT      = 3;
const uint32_t METEOR_WEAK_WINDOW_MS  = 60000;
const uint32_t METEOR_READ_MS         = 100;    // опрос MPU6050: удар короткий, читаем часто
const int      METEOR_CONFIRM_READS   = 2;      // пик подтверждаем N отсчётами подряд
const uint32_t METEOR_COOLDOWN_MS     = 1500;   // пауза после пика: один удар считается один раз
const uint32_t METEOR_ALARM_HOLD_MS   = 30000;  // сколько держится АВАРИЯ после сильного удара
const int      METEOR_WEAK_STORE      = 10;     // сколько слабых ударов помним для окна 60 с
// Автореакция: при АВАРИИ — «Проверить герметичность» на пульте и сигнал в №3.

// ======== Уровни тревоги, интерфейс подсистемы, утилиты ========

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

// ---- Пороги и защита от ложных тревог — для любой подсистемы ----

// Границы одного параметра. Нет границы — ±INFINITY.
struct Band {
  float alarmLo, warnLo, warnHi, alarmHi;
  float hyst;  // возврат на уровень ниже — только с этим запасом
};

// Уровень по значению. Гистерезис: пока параметр на уровне cur, граница этого уровня
// сдвинута внутрь нормы на hyst, поэтому дребезг около порога не меняет уровень туда-обратно.
inline AlarmLevel classify(float v, const Band& b, AlarmLevel cur) {
  float a = cur == LVL_ALARM ? b.hyst : 0;
  if (v < b.alarmLo + a || v > b.alarmHi - a) return LVL_ALARM;
  float w = cur >= LVL_WARN ? b.hyst : 0;
  if (v < b.warnLo + w || v > b.warnHi - w) return LVL_WARN;
  return LVL_OK;
}

// Выдержка: подтверждённый уровень меняется, только если новый держится HOLD_MS.
// raw — уровень по свежему показанию (classify или своё условие, например «оба признака»).
struct LevelHold {
  AlarmLevel level = LVL_OK;      // подтверждённый — его показываем на пульте
  AlarmLevel candidate = LVL_OK;  // новый, ещё не продержавшийся HOLD_MS
  uint32_t   candidateSinceMs = 0;

  AlarmLevel update(AlarmLevel raw, uint32_t nowMs) {
    if (raw == level) {
      candidate = level;
    } else if (raw != candidate) {
      candidate = raw;
      candidateSinceMs = nowMs;
    } else if (nowMs - candidateSinceMs >= HOLD_MS) {
      level = raw;
    }
    return level;
  }
};
