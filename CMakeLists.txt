cmake_policy(SET CMP0048 NEW)

project(river VERSION 0.0.1)
set(PACKAGE_VERSION 0.0.1)

cmake_minimum_required(VERSION 3.13)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules/")

add_subdirectory(src)
add_subdirectory(python)

if (DEFINED RIVER_BUILD_INGESTER AND RIVER_BUILD_INGESTER)
    add_subdirectory(ingester)
endif()
