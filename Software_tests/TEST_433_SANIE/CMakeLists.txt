cmake_minimum_required(VERSION 3.22)

# ESP-IDF/Cursor uses this folder as cmake -S. The firmware lives in test1_rx_idf/.
if(NOT DEFINED ENV{IDF_PATH} OR "$ENV{IDF_PATH}" STREQUAL "")
  set(ENV{IDF_PATH} "/home/hernan/esp/esp-idf")
endif()

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/test1_rx_idf/main")
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/test1_rx_idf/sdkconfig.defaults")
set(SDKCONFIG "${CMAKE_CURRENT_LIST_DIR}/test1_rx_idf/sdkconfig")

include("$ENV{IDF_PATH}/tools/cmake/project.cmake")
idf_build_set_property(MINIMAL_BUILD ON)
project(test1_rx_idf)
