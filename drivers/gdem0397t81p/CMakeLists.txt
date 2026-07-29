# drivers/gdem0397t81p/CMakeLists.txt
idf_component_register(
    SRCS "Display_EPD_W21.c"
    INCLUDE_DIRS "." "../../boards/esp32s3"
    REQUIRES driver
)
