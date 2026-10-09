# Driver migration map

Drivers are organized by hardware device or capability, never by application
name. `lxj_` is reserved for migrated applications under `apps/`.

| Reference component | Target driver ownership | Status |
| --- | --- | --- |
| `aw9523` | `drivers/aw9523` | Existing driver; compare before changing |
| `epaper_lib`, `epaper_port` | `drivers/gdem0397t81p` + display controller | Existing path; do not duplicate |
| `button_bsp` | `system/input` and touch driver | Existing input path |
| `esp_wifi_bsp` | `system/wifi` | Existing service |
| `sdcard_bsp` | `system/controller/sd_control` | Existing service |
| `pcf85063_bsp` | `drivers/pcf85063` + `system/timeservice` | Register driver migrated; time policy unchanged |
| `i2c_bsp` | shared IDF I2C ownership | No standalone duplicate |
| `axpPower` | `drivers/axp2101` | Register driver migrated; power policy remains in system/battery |
| `qmi8658_bsp` | `drivers/qmi8658` | Device driver migrated; no runtime consumer enabled |
| `shtc3_bsp` | `drivers/shtc3` | Device driver migrated; no runtime consumer enabled |
| `es8311_bsp` | `drivers/es8311` | Control driver migrated; I2S/audio backend remains unchanged |

New driver code must have one owner for bus initialization and must be consumed
by a system service before it is enabled in the build.

## Current board note

The reference sensor bus uses GPIO17/18, while the current DeepStoa board
definition assigns GPIO41/42 to the FT6336 touch bus. The migrated device
drivers therefore remain attach-only libraries. Enabling them in a system
service requires a confirmed schematic/pin mapping and an explicit shared-bus
owner first.
