# debug/t_quality/main/CMakeLists.txt
# epd_display.c is shared with debug/t_display; referenced in place, not copied.
idf_component_register(
    SRCS "t_quality.c" "../../t_display/main/epd_display.c"
    INCLUDE_DIRS "." "../../t_display/main"
                 "../../../boards/esp32s3"
    REQUIRES driver esp_timer
    PRIV_REQUIRES ft6336
)

# Board: ESP32-S3 DevKit (boards/esp32s3/esp32s3_devkit.h)
#   MOSI=11 SCK=12 CS=15 DC=6 RST=7 BUSY=8, SPI2
#   Touch: I2C0 SDA=41 SCL=42 RST=5 (FT6336U @ 0x38)
#
# SPI runs at the driver default (10 MHz, matching the Arduino reference).
# To probe the bus with a 24 MS/s logic analyzer (debug/la_tools) again, add
#   EPD_SPI_CLOCK_HZ=2000000
# below — but note that costs ~5x on transfer time.
