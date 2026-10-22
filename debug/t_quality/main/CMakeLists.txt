# debug/t_quality/main/CMakeLists.txt
# DevKit display transport is local to avoid changing t_display's DeepStoa v1 wiring.
idf_component_register(
    SRCS "t_quality.c" "epd_display.c"
    INCLUDE_DIRS "." "../../../boards/esp32s3"
    REQUIRES driver ft6336
)

# Board: ESP32-S3 DevKit (boards/esp32s3/esp32s3_devkit.h)
# SPI clock lowered to 2 MHz so a 24 MS/s logic analyzer (debug/la_tools) can decode the bus.
# Remove EPD_SPI_CLOCK_HZ to go back to the driver default (10 MHz).
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    EPD_SPI_CLOCK_HZ=2000000
)
