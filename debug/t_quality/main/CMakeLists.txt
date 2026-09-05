# debug/t_quality/main/CMakeLists.txt
# epd_display.c is shared with debug/t_display; referenced in place, not copied.
idf_component_register(
    SRCS "t_quality.c" "../../t_display/main/epd_display.c"
    INCLUDE_DIRS "." "../../t_display/main"
                 "../../../boards/esp32s3"
                 "../../../boards/heltec_wireless_stick_v3"
    REQUIRES driver
)

# Board: Heltec Wireless Stick V3 (boards/heltec_wireless_stick_v3/heltec_wireless_stick_v3.h)
# SPI clock lowered to 2 MHz so a 24 MS/s logic analyzer (debug/la_tools) can decode the bus.
# Remove EPD_SPI_CLOCK_HZ to go back to the driver default (10 MHz).
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    EPD_BOARD_HELTEC_WIRELESS_STICK_V3
    EPD_SPI_CLOCK_HZ=2000000
)
