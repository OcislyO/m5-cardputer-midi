# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# bsp/drv/sys/app are grouping directories, not components themselves -- each
# one is registered here so its subdirectories (bsp_i2c, sys_nvs, app_core, ...)
# are picked up as individual components.
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/components/bsp
    ${CMAKE_CURRENT_LIST_DIR}/components/drv
    ${CMAKE_CURRENT_LIST_DIR}/components/sys
    ${CMAKE_CURRENT_LIST_DIR}/components/app
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(m5_midi)
