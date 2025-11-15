cmake_minimum_required(VERSION 3.22)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
execute_process(
	COMMAND ${CMAKE_COMMAND} -E create_symlink
			${CMAKE_BINARY_DIR}/compile_commands.json compile_commands.json)

list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_LIST_DIR}/modules/dsmr_p1)

find_package(Zephyr REQUIRED PATHS $ENV{ZEPHYR_BASE})
project(p1_http_server)

target_sources(app 
    PRIVATE 
        src/main.c
        src/tcp/server.c
        src/http/server.c
        src/http/encoder.c
)

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated/)
foreach(web_resource
  index.html
  main.js
  favicon.ico
    )
  generate_inc_file_for_target(
    app
    src/web/${web_resource}
    ${gen_dir}/${web_resource}.gz.inc
    --gzip
  )
endforeach()
