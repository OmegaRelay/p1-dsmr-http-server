
zephyr_library()
zephyr_include_directories(include)
zephyr_library_sources_ifdef(CONFIG_DSMR_P1 dsmr_p1.c)
