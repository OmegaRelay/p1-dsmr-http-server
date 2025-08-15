cmake_minimum_required(VERSION 3.22)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_LIST_DIR}/modules/dsmr_p1)

find_package(Zephyr REQUIRED PATHS $ENV{ZEPHYR_BASE})
project(p1_http_server LANGUAGES C)

target_sources(app PRIVATE src/main.c)
