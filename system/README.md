# System migration map

Long-lived device capabilities live under `system/services/`. The root keeps
framework boundaries (`appmgr`, `events`, `launcher`, `uilv`) and hardware
control (`controller`). `input` and `sleep` are retained but not enabled in
the firmware build.

- Display: `system/controller/display_control`
- Storage: `system/controller/sd_control`, `fs_control`
- Time: `system/services/time`
- Weather data: `system/services/weather_service`
- Alarm/timer: `system/services/alarm`
- Wi-Fi: `system/services/wifi`
- Audio: `system/services/audio`
- Battery: `system/services/battery`
- OTA: `system/services/ota`
- Flash settings: `system/services/flash`
- Logs and diagnostics: `system/services/logs`, `system/services/monitor`
- App lifecycle: `system/appmgr`

Reference BSPs are adapted into these services only when a migrated feature
needs them. Application code must not initialize a sensor, bus, display, or
codec directly.
