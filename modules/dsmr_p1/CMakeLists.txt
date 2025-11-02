
zephyr_include_directories(include)
if(CONFIG_DSMR_P1)
zephyr_library()
zephyr_library_sources(src/dsmr_p1.c)
zephyr_library_sources(src/zephyr/platform.c)
endif(CONFIG_DSMR_P1)
