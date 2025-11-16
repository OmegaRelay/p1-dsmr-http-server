
function(${SYSBUILD_CURRENT_MODULE_NAME}_pre_cmake)
    cmake_parse_arguments(PRE_CMAKE "" "" "IMAGES" ${ARGN})

    foreach(image ${PRE_CMAKE_IMAGES})
        if (SB_CONFIG_BUILD_OUTPUT_HEX)
            set_config_bool(${image} CONFIG_BUILD_OUTPUT_HEX y)
        else()
            set_config_bool(${image} CONFIG_BUILD_OUTPUT_HEX n)
        endif()
    endforeach()
endfunction()

