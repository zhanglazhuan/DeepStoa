# drivers/ft6336/CMakeLists.txt
idf_component_register(
    SRCS "ft6336.c"
    INCLUDE_DIRS "."
    REQUIRES driver
)
