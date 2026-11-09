# Changelog

## 3.0.0 - first release of v2

### Added
- Standalone mode: the CYD reads MakerWorld by itself, no browser extension needed (`/config` page)
- Exact profile totals for Downloads, Prints, Boosts and Likes
- Telegram notifications sent by the CYD, with the model cover photo
- Home Assistant via MQTT with auto discovery (`/mqtt` page): sensors and an "Activity" event entity
- Notification queue: simultaneous events are shown one after the other
- New high-contrast interface with bold fonts
- CYD 2.4" (ESP32-2432S024) firmware: backlight on GPIO 27, screen rotated 180°
- `/screenshot` endpoint (BMP of the display)
- Halo case for CYD 2.4" and 2.8", 3D files on MakerWorld: https://makerworld.com/en/models/3577289-makerworld-live-monitor-v2

### Fixed (from v1)
- Notifications overwriting each other
- Automatic recovery when the WiFi is connected but the network does not work

### Removed
- The browser extension is no longer part of this repository (v1 is still available at
  https://github.com/aquascape123/makerworld-live-monitor)
