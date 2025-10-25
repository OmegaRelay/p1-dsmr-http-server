cmake_minimum_required(VERSION 3.22)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
execute_process(
	COMMAND ${CMAKE_COMMAND} -E create_symlink
			${CMAKE_BINARY_DIR}/compile_commands.json compile_commands.json)

list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_LIST_DIR}/modules/dsmr_p1)

find_package(Zephyr REQUIRED PATHS $ENV{ZEPHYR_BASE})
project(p1_http_server LANGUAGES C)

zephyr_linker_sources(SECTIONS sections-rom.ld)
zephyr_linker_section(NAME
				http_resource_desc_http_service
				KVMA RAM_REGION GROUP RODATA_REGION
				SUBALIGN Z_LINK_ITERABLE_SUBALIGN)

FILE(GLOB app_sources 
    src/*.c 
)

target_sources(app PRIVATE ${app_sources})
