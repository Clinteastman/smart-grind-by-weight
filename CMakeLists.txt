cmake_minimum_required(VERSION 3.16.0)

# The firmware sources in src/ form the application component. Building only
# that component and what it requires keeps unused ESP-IDF components, and the
# startup hooks some of them register, out of the image.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/src")
set(COMPONENTS src)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# LVGL ships as a managed component and locates lv_conf.h through
# LV_CONF_INCLUDE_SIMPLE, so the project's include/ directory has to be on the
# search path of every component - not just the firmware sources.
idf_build_set_property(COMPILE_OPTIONS "-I${CMAKE_CURRENT_LIST_DIR}/include" APPEND)

project(smart-grind-by-weight)
