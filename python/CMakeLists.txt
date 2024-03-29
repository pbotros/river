project(riverpy)
cmake_minimum_required(VERSION 3.13)
file(STRINGS "../VERSION" PACKAGE_VERSION)

option(RIVER_INSTALL "Set to ON to install River (set to OFF if embedding in another project)" ON)

if (NOT Python3_EXECUTABLE)
  find_package (Python3 COMPONENTS Interpreter Development)
endif()

find_package (river REQUIRED)

set(SETUP_PY_IN "${CMAKE_CURRENT_SOURCE_DIR}/setup.py.in")
set(SETUP_PY    "${CMAKE_CURRENT_BINARY_DIR}/setup.py")

get_target_property(RIVER_INCLUDE_DIRS river::river INTERFACE_INCLUDE_DIRECTORIES)
configure_file(${SETUP_PY_IN} ${SETUP_PY})

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/exception_handling.h" "${CMAKE_CURRENT_BINARY_DIR}/exception_handling.h" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/river.pyx" "${CMAKE_CURRENT_BINARY_DIR}/river.pyx" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/criver.pxd" "${CMAKE_CURRENT_BINARY_DIR}/criver.pxd")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/river.pyi" "${CMAKE_CURRENT_BINARY_DIR}/river.pyi" COPYONLY)

if (RIVER_INSTALL)
  install(CODE "execute_process(
          COMMAND ${Python3_EXECUTABLE} setup.py install
          WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})")
endif()

add_custom_target(riverpy ALL DEPENDS river)
