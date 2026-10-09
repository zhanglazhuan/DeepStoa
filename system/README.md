# System migration map

System code is organized by long-lived service responsibility, not by the
source project or application prefix.

- Display: `system/controller/display_control`
- Storage: `system/controller/sd_control`, `fs_control`
- Time: `system/timeservice`
- Alarm/timer: `system/alarm`
- Wi-Fi: `system/wifi`
- Audio: `system/audio`
- App lifecycle: `system/appmgr`

Reference BSPs are adapted into these services only when a migrated feature
needs them. Application code must not initialize a sensor, bus, display, or
codec directly.
