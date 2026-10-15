cmake_minimum_required(VERSION 3.16)

# Build only this diagnostic and its dependencies.
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(deepstoa_t_key)
