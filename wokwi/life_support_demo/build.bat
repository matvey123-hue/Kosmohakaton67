@echo off
rem Build subsystem 1 demo for Wokwi VS Code. Needs Arduino IDE 2 (bundles arduino-cli) and the esp32 core.
cd /d "%~dp0"

rem Logic lives in firmware/, copy it next to the sketch
copy /y ..\..\firmware\life_support.h . >nul || exit /b 1
copy /y ..\..\firmware\life_support.cpp . >nul || exit /b 1
copy /y ..\..\firmware\alarm_level.h . >nul || exit /b 1

set "CLI=arduino-cli"
where arduino-cli >nul 2>nul || set "CLI=%LOCALAPPDATA%\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"

"%CLI%" lib install "Adafruit ILI9341" "U8g2_for_Adafruit_GFX" "DHT sensor library" "OneWire" "DallasTemperature" "ESP32Servo" || exit /b 1
"%CLI%" compile -b esp32:esp32:esp32 --output-dir build . || exit /b 1
echo OK: build\life_support_demo.ino.merged.bin
