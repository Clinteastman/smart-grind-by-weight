# The firmware sources form the project's application component, which the root
# CMakeLists.txt registers. Third-party dependencies are declared in
# idf_component.yml.
# CONFIGURE_DEPENDS re-runs the glob when a source file is added or removed,
# so an existing build directory does not need a manual reconfigure. ESP-IDF
# first evaluates this file in script mode to collect requirements, where that
# option is not allowed; the source list is unused in that pass.
if(CMAKE_BUILD_EARLY_EXPANSION)
    set(app_sources "")
else()
    file(GLOB_RECURSE app_sources CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_LIST_DIR}/*.c"
         "${CMAKE_CURRENT_LIST_DIR}/*.cpp")
endif()

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "."
    REQUIRES
        # Espressif components
        app_update
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_rmt
        esp_driver_spi
        esp_driver_usb_serial_jtag
        esp_event
        esp_http_client
        esp_http_server
        esp_https_ota
        esp_lcd
        esp_netif
        esp_partition
        esp_timer
        esp_wifi
        esp-tls
        esptool_py
        lwip
        mbedtls
        nvs_flash
        spi_flash
        vfs
        # Managed components
        esp_delta_ota
        esp_lcd_co5300
        esp_lcd_sh8601
        esp_websocket_client
        improv
        littlefs
        lvgl
        mdns
        esp-nimble-cpp
)

# The generated LVGL font sources in src/font pick their lvgl.h include style
# from this macro.
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)

# The firmware builds at the optimisation level sdkconfig.defaults selects, like
# every ESP-IDF component, with ESP-IDF's -Wall -Wextra. Do not add -Ofast: its
# -ffinite-math-only lets the compiler drop the std::isfinite checks that
# reject NaN settings from the web API and NVS.

# config/git_info.h carries the build number and git commit. It is refreshed
# before every build; the script leaves an unchanged header alone, so an
# unchanged tree still builds incrementally.
idf_build_get_property(python PYTHON)
add_custom_target(smart_grind_build_info
    COMMAND "${python}" "${CMAKE_CURRENT_LIST_DIR}/../tools/build-scripts/build_info.py"
    BYPRODUCTS "${CMAKE_CURRENT_LIST_DIR}/config/git_info.h"
    COMMENT "Updating build number and git information"
    VERBATIM)
add_dependencies(${COMPONENT_LIB} smart_grind_build_info)
