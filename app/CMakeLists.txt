cmake_minimum_required(VERSION 3.22)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "")

# list(APPEND BOARD_ROOT ${CMAKE_SOURCE_DIR})
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_LIST_DIR}/modules/dsmr_p1)

find_package(Zephyr REQUIRED PATHS $ENV{ZEPHYR_BASE})
project(office-intercom LANGUAGES C)

FILE(GLOB app_sources 
    src/*.c 
)

target_sources(app PRIVATE ${app_sources})
