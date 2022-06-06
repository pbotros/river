find_package (Python3 COMPONENTS Interpreter Development)

set(SETUP_PY_IN "${CMAKE_CURRENT_SOURCE_DIR}/setup.py.in")
set(SETUP_PY    "${CMAKE_CURRENT_BINARY_DIR}/setup.py")

configure_file(${SETUP_PY_IN} ${SETUP_PY})

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/river.pyx" "${CMAKE_CURRENT_BINARY_DIR}/river.pyx" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/criver.pxd" "${CMAKE_CURRENT_BINARY_DIR}/criver.pxd")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/river.pyi" "${CMAKE_CURRENT_BINARY_DIR}/river.pyi" COPYONLY)

if (RIVER_INSTALL)
  install(CODE "execute_process(
          COMMAND ${Python3_EXECUTABLE} setup.py install
          WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})")
endif()

add_custom_target(riverpy ALL DEPENDS river)
