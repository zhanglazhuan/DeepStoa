# LXJ Weather BMP assets

Weather images migrated from
`refer/esp32_paper-v2.0/main/page_weather/Weather_img`.

These files are kept under the UI image tree as source/runtime BMP assets.
Their generated `L8` LVGL descriptors are in the sibling `icon_weather`
directory; the `lxj_weather` page displays the `clear` descriptor as its safe
offline preview. The weather data service remains a separate, board/network
dependent follow-up.

| File | Meaning |
| --- | --- |
| `clear.bmp` | clear |
| `overcast.bmp` | overcast |
| `partly_cloudy.bmp` | partly cloudy |
| `light_rain.bmp`, `moderate_rain.bmp`, `heavy_rain.bmp` | rain |
| `rainstorm.bmp`, `thunderstorm.bmp` | storm |
| `snow.bmp` | snow |
| `haze.bmp` | haze |
| `dust_storm.bmp` | dust storm |
| `gale.bmp` | gale |
