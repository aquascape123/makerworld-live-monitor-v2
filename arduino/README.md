# Firmware

| File | Content |
|---|---|
| `makerworld_monitor_cyd.ino` | setup, loop, WiFi, buzzer, LED, v1 endpoints |
| `standalone.ino` / `standalone.h` | reads MakerWorld, `/config` page, Telegram |
| `mqtt.ino` | Home Assistant / MQTT, `/mqtt` page |
| `ui.ino` | screens and notifications |
| `ui_cover.ino` | model cover photo in the notifications |
| `screenshot.ino` | `/screenshot` endpoint |

Board: **ESP32 Dev Module**. Libraries: see `libraries_required.txt`.
For the CYD 2.4" compile with `CYD_24` defined. TFT_eSPI settings are in the main README.
