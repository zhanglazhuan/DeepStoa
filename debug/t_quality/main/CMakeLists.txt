# debug/t_quality/main/CMakeLists.txt
# DevKit display transport is local to avoid changing t_display's DeepStoa v1 wiring.
idf_component_register(
    SRCS "t_quality.c" "epd_display.c"
    INCLUDE_DIRS "." "../../../boards/esp32s3"
    REQUIRES driver ft6336
)

# Board: DeepStoa v1 (boards/esp32s3/deepstoa_v1.h)
#   EPD: MOSI=10 SCK=11 CS=12 BUSY=21, DC/RST/POWER via AW9523
#   Touch: I2C0 SDA=17 SCL=18, RST via AW9523 (FT6336U @ 0x38)
#
# SPI runs at the driver default (10 MHz, matching the Arduino reference).
# To probe the bus with a 24 MS/s logic analyzer (debug/la_tools) again, add
#   EPD_SPI_CLOCK_HZ=2000000
# below — but note that costs ~5x on transfer time.
