# LXJ app migration status

All migrated applications are registered independently from the existing apps and
use the `LXJ ` display-name prefix. Original sources remain under each app's
`legacy/` directory. The eight small compatibility pages currently share the
`lxj` compatibility component and are registered through their existing
`lxj_*_init()` app entry points.

| App | Current integration |
| --- | --- |
| `lxj_clock` | Current time service and LVGL page |
| `lxj_todolist` | Legacy CSV import, independent storage, toggle UI |
| `lxj_picture` | SD card image-file listing |
| `lxj_audio` | Current audio service and SD card music listing |
| `lxj_alarm` | Current alarm service, alarm list and enable toggle |
| `lxj_pomodoro` | Monotonic-clock 25-minute timer with pause/reset and completion state |
| `lxj_network` | Async scan result list, reconnect/disconnect controls and current Wi-Fi status |
| `lxj_settings` | Current Wi-Fi, storage, battery and time status; 12/24h toggle and SNTP sync |
| `lxj_fiction` | SD card library with streaming TXT reader, paging controls and isolated EPUB pending state |
| `lxj_mistakebook` | Parses legacy `cards.csv`, selectable/detail view, previous/next navigation and atomic review-state persistence |
| `lxj_chat` | Shared Chatbot lifecycle alias with independent LXJ registration name |
| `lxj_weather` | Async HTTP/JSON weather fetch, city dropdown and `icon_weather` image mapping |

The legacy pages are intentionally not compiled into the active app components.
They are preserved as migration references until an equivalent current service
exists for any remaining hardware-specific behavior.

## Functional-equivalence follow-up

The entries below are registered and launchable, but are not yet equivalent to
the reference applications. They remain explicitly in the compatibility stage:

| App | Next implementation target | Completion gate |
| --- | --- | --- |
| `lxj_settings` | Independent LVGL settings flow using current Wi-Fi, time, storage, battery and OTA services | Read/write settings, reboot/reset guards, and persistence survive app restart |
| `lxj_network` | Wi-Fi scan, connect/disconnect, saved-network state and connection feedback | No blocking work on LVGL thread; state transitions and failures visible |
| `lxj_pomodoro` | Match reference timer modes, pause/reset and completion notification while retaining current timer ownership | Timer survives page refresh safely and does not interfere with system alarms |
| `lxj_mistakebook` | CSV model, selectable cards, answer/detail view and progress persistence | Malformed rows are skipped safely; edits do not corrupt the source CSV |
| `lxj_weather` | HTTP/JSON service, city selection, weather mapping and `icon_weather` selection | Offline/error states are visible; network work is asynchronous |
| `lxj_chat` | Reuse the existing Chatbot session/history/audio services behind the LXJ entry | One session/audio owner; no duplicate storage or playback task |
| `lxj_fiction` | Shared EPUB reader service, resume position and richer text layout | TXT is streamed and paginated; EPUB legacy renderer is not linked |

Implementation order is Settings/Network, Pomodoro, Mistakebook, Weather,
Chat, then Fiction. Each stage keeps the current app available, is compiled
and smoke-tested before the next stage, and does not remove the preserved
`legacy/` source.
