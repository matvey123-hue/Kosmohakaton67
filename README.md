# Kosmohakaton67

Система мониторинга и аварийного оповещения лунной базы: ESP32 в симуляторе Wokwi.
Четыре подсистемы — жизнеобеспечение, пожарная безопасность, шлюз, метеоритная защита —
три уровня тревоги, общий пульт и журнал. Хакатон «CubeSat на STM32, ESP32 и Arduino».

Всё о проекте — пороги, распиновка, роли, правила кода — в [spec.html](spec.html).

## Где что лежит

```
spec.html            спека
wokwi/main/          проект Wokwi целиком, без подпапок
  main.ino           пины, общие части (журнал, пульт, звук), связи подсистем
  common.h           AlarmLevel, интерфейс Subsystem, общие константы
  1_life_support.*   №1 Жизнеобеспечение
  2_fire.*           №2 Пожарная безопасность
  3_airlock.*        №3 Шлюз и герметичность
  4_meteor.*         №4 Метеоритная защита
  diagram.json       схема
  libraries.txt      библиотеки для онлайн-Wokwi
  wokwi.toml         путь к прошивке для Wokwi в VS Code
```

Границы файлов и правила для кода подсистем — спека, раздел 8.

## Запуск локально

Нужны VS Code с расширением Wokwi и Arduino IDE 2 (в ней есть `arduino-cli`) с ядром esp32.

1. Один раз поставить библиотеки из `wokwi/main/libraries.txt`:
   `arduino-cli lib install "<название>"` для каждой строки.
2. Собрать прошивку в папке `wokwi/main`:
   ```
   arduino-cli compile -b esp32:esp32:esp32 --output-dir build .
   ```
   Если `arduino-cli` нет в PATH, он лежит в
   `%LOCALAPPDATA%\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe`.
3. Открыть папку `wokwi/main` в VS Code, F1 → «Wokwi: Start Simulator».
   Показания датчиков меняются кликом по ним во время симуляции.

После правки кода — снова шаг 2 и перезапуск симулятора.

## Сдача

Проект сдаётся ссылкой на онлайн-Wokwi (wokwi.com): файлы из `wokwi/main/` переносятся туда вручную,
`libraries.txt` подтягивает библиотеки. Срок — 17.10.2026, 15:00 МСК.
