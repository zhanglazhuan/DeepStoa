cmake_minimum_required(VERSION 3.16)
set(IDF_TARGET esp32s3)
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../drivers/aw9523"
    "${CMAKE_CURRENT_LIST_DIR}/../../drivers/board_audio"
    "${CMAKE_CURRENT_LIST_DIR}/../../drivers/gdem0397t81p"
)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(deepstoa_t_stress)
