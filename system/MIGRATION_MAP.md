# Reference system migration map

This is the expand stage of the system migration. Existing DeepStoa services
remain authoritative; no legacy service is removed or replaced by this map.

| Reference responsibility | Current reader(s) | Current writer/owner | Migration action | Rollback |
| --- | --- | --- | --- | --- |
| Clock/RTC | clock, alarm, status bar | `system/timeservice` | Keep software/SNTP time policy; `drivers/pcf85063` is an optional backend through `time_service_pcf85063_backend_init()` and `time_service_set_rtc_backend()` | Do not register RTC; current time service continues |
| Alarm/timer | clock, LXJ alarm/pomodoro | `system/alarm` | Reuse existing alarm model and event bus | Disable LXJ controls; system alarm remains |
| Wi-Fi lifecycle | settings, network, weather/chat status | `system/wifi` | Reuse manager/store; no reference Wi-Fi init | Do not call legacy BSP |
| SD/filesystem | audio, picture, fiction, todolist, mistakebook | `system/controller/fs_control` and `sd_control` | Keep one mount and one file owner | Remove LXJ app registration only |
| Audio session | player, LXJ audio, chatbot audio boundary | `system/audio` | Keep session/queue owner; ES8311 control driver is not an audio backend yet | Continue mock `audio_out` |
| Display refresh | all LVGL apps | `system/controller/display_control` | Keep LVGL/e-paper refresh policy | Legacy pages stay uncompiled |
| Physical input | launcher, sleep, apps | `system/input` and touch controller | Keep managed button/touch ownership | No reference `button_bsp` init |
| Battery/power | status bar, sleep/settings | `system/battery` | Keep ADC-based battery policy; optional `battery_backend_t` allows AXP gauge data without moving policy or rail writes | Do not register backend; ADC policy continues |
| IMU/environment sensors | no current reader | none | Drivers remain unbound until board wiring and a consumer exist | No runtime initialization |

## Pending reference modules

| Reference module | Current state | Reason it is not enabled yet |
| --- | --- | --- |
| `main/file_browser` | Functionally migrated | `system/uilv/widgets/lv_folder_selector` plus Reader/Anki/Fiction file pages provide the current LVGL equivalent; the legacy e-paper renderer is intentionally not copied |
| `main/usb_msc` | Interface not enabled | Current storage owner is internal FAT at `/sdcard`; SDMMC pins and USB device-mode ownership are not confirmed, so enabling the reference manager would risk a second storage owner |
| `main/protocols` | Not migrated | No current consumer, endpoint contract, or ownership boundary identified; copying MQTT/WebSocket code would add an unowned network session |
| `main/led` | Not migrated as a reference module | Current board LED pin and state-owner contract are not confirmed; existing vibration/status paths remain authoritative |
| `system/input` | Source retained, not built | Existing source requires `iot_button`/`button_gpio` and a board-specific key map; the current board definition only confirms display/touch pins |
| `system/sleep` | Source retained, not built | Requires a verified wake source and must be adapted to the current e-paper display-control API; enabling it now could leave the device asleep without a wake path |

## Compatibility rules

1. A new driver may be compiled without being initialized at boot.
2. A system service may claim a device only after its bus owner and board pins
   are confirmed.
3. Existing service APIs and persistent data remain valid during expansion.
4. Legacy source is preserved; contraction/removal requires a separate change.

## Verification gates

- Build the complete firmware after each service integration.
- Confirm exactly one owner for each bus, mount point, audio session, and time
  source.
- Keep the old service path available until the new path has a real consumer
  and hardware validation.
