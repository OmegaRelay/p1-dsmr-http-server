
zephyr_library()
zephyr_include_directories(include)
zephyr_library_sources_ifdef(CONFIG_DSMR_P1 src/dsmr_p1.c)
zephyr_library_sources_ifdef(CONFIG_DSMR_P1 src/zephyr/platform.c)
