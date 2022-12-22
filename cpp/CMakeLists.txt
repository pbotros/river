cmake_policy(SET CMP0048 NEW)
cmake_policy(SET CMP0077 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
cmake_minimum_required(VERSION 3.13)


file(STRINGS "../VERSION" PACKAGE_VERSION)
project(river VERSION ${PACKAGE_VERSION})


set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules/")

option(RIVER_BUILD_TESTS "Set to ON to build tests" ON)
option(RIVER_BUILD_INGESTER "Set to ON to build the ingester" OFF)
option(RIVER_INSTALL "Set to ON to install River (set to OFF if embedding in another project)" ON)

add_subdirectory(src)

if (RIVER_BUILD_INGESTER)
    add_subdirectory(ingester)
endif()

if (RIVER_BUILD_TESTS)
  set(INSTALL_GTEST OFF)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY    https://github.com/google/googletest.git
    GIT_TAG           release-1.12.1
  )
  FetchContent_MakeAvailable(googletest)
  enable_testing()
endif()
