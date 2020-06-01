cmake_minimum_required(VERSION 3.13)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/Modules/")
set(PACKAGE_VERSION 0.0.1)

add_subdirectory(src)
