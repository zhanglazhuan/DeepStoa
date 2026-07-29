# drivers/aw9523/CMakeLists.txt
idf_component_register(
    SRCS "aw9523.c"
    INCLUDE_DIRS "."
    REQUIRES driver
)
