idf_component_register(
  SRCS "i2c_bsp.c"
  REQUIRES driver
  INCLUDE_DIRS "./")

if(CONFIG_BOARD_TYPE_WAVESHARE_S3_PHOTOPAINT_V2 AND CONFIG_PHOTOPAINT_ES7210_SWAPPED_I2C)
  include("${CMAKE_CURRENT_LIST_DIR}/pin_swap.cmake")
endif()
