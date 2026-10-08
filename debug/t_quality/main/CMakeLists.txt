# debug/t_quality/main/CMakeLists.txt
# epd_display.c is shared with debug/t_display; referenced in place, not copied.
idf_component_register(
    SRCS "t_quality.c" "../../t_display/main/epd_display.c"
    INCLUDE_DIRS "." "../../t_display/main"
                 "../../../boards/esp32s3"
    REQUIRES driver esp_timer
    PRIV_REQUIRES aw9523 ft6336
)

# Board: DeepStoa v1 (boards/esp32s3/deepstoa_v1.h)
#   EPD: MOSI=10 SCK=11 CS=12 BUSY=21, DC/RST/POWER via AW9523
#   Touch: I2C0 SDA=17 SCL=18, RST via AW9523 (FT6336U @ 0x38)
#
# SPI runs at the driver default (10 MHz, matching the Arduino reference).
# To probe the bus with a 24 MS/s logic analyzer (debug/la_tools) again, add
#   EPD_SPI_CLOCK_HZ=2000000
# below — but note that costs ~5x on transfer time.
