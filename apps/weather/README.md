# Weather

`app.c` owns the
lifecycle, `model.c` owns the city selection and current result, `controller.c`
starts requests and consumes results, and `view.c` composes a `lv_page` screen.

HTTP and JSON parsing run in `system/services/weather_service`, on a background task.
The LVGL controls and weather icon mapping are in
`system/uilv/widgets/lv_weather_panel`. The app polls the service result queue
from the LVGL thread, so a result arriving after the app closes cannot access
its deleted controls. Request tokens discard stale results after reopening.
