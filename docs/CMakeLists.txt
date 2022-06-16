cmake_policy(SET CMP0048 NEW)
cmake_policy(SET CMP0077 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
cmake_minimum_required(VERSION 3.13)

file(STRINGS "../VERSION" PACKAGE_VERSION)
project(river-docs VERSION ${PACKAGE_VERSION})

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}")

find_package(Doxygen REQUIRED)
find_package(Sphinx REQUIRED)
find_package(river REQUIRED)

# Find all the public headers
get_target_property(RIVER_PUBLIC_HEADER_DIR river::river INTERFACE_INCLUDE_DIRECTORIES)
file(GLOB_RECURSE RIVER_PUBLIC_HEADERS ${RIVER_PUBLIC_HEADER_DIR}/*.h)

set(DOXYGEN_INPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../cpp/src)
set(DOXYGEN_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/cpp/doxygen)
set(DOXYGEN_INDEX_FILE ${DOXYGEN_OUTPUT_DIR}/cpp/html/index.html)
set(DOXYFILE_IN ${CMAKE_CURRENT_SOURCE_DIR}/api/cpp/Doxyfile.in)
set(DOXYFILE_OUT ${CMAKE_CURRENT_BINARY_DIR}/api/cpp/Doxyfile)

set(RST_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/index.rst
  ${CMAKE_CURRENT_SOURCE_DIR}/install.rst
  ${CMAKE_CURRENT_SOURCE_DIR}/tutorial.rst
  ${CMAKE_CURRENT_SOURCE_DIR}/performance.rst
  ${CMAKE_CURRENT_SOURCE_DIR}/contributing.rst
  ${CMAKE_CURRENT_SOURCE_DIR}/api/index.rst
  ${CMAKE_CURRENT_SOURCE_DIR}/api/cpp.rst
  ${CMAKE_CURRENT_SOURCE_DIR}/api/python.rst
)

###############################
# C++: Using Doxygen to Parse #
###############################
# Replace variables inside @@ with the current values
configure_file(${DOXYFILE_IN} ${DOXYFILE_OUT} @ONLY)

# Doxygen won't create this for us
file(MAKE_DIRECTORY ${DOXYGEN_OUTPUT_DIR})

# Only regenerate Doxygen when the Doxyfile or public headers change
add_custom_command(OUTPUT ${DOXYGEN_INDEX_FILE}
                   DEPENDS ${RIVER_PUBLIC_HEADERS}
                   COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
                   MAIN_DEPENDENCY ${DOXYFILE_OUT} ${DOXYFILE_IN}
                   COMMENT "Generating docs"
                   VERBATIM)

# Nice named target so we can run the job easily
add_custom_target(Doxygen ALL DEPENDS ${DOXYGEN_INDEX_FILE})

#################################
# Python & C++: Using Sphinx ####
#################################
set(SPHINX_SOURCE ${CMAKE_CURRENT_SOURCE_DIR})
# All changes should actually edit the *source* directories.
set(SPHINX_BUILD ${CMAKE_CURRENT_SOURCE_DIR}/docs)
set(SPHINX_INDEX_FILE ${SPHINX_BUILD}/index.html)

# Only regenerate Sphinx when:
# - Doxygen has rerun
# - Our doc files have been updated
# - The Sphinx config has been updated
add_custom_command(OUTPUT ${SPHINX_INDEX_FILE}
                   COMMAND 
                     ${SPHINX_EXECUTABLE} -b html
                     # Tell Breathe where to find the Doxygen output
                     -Dbreathe_projects.river=${DOXYGEN_OUTPUT_DIR}/xml
                   ${SPHINX_SOURCE} ${SPHINX_BUILD}
                   WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                   DEPENDS
                   ${RST_FILES}
                   ${DOXYGEN_INDEX_FILE}
                   MAIN_DEPENDENCY ${SPHINX_SOURCE}/conf.py
                   COMMENT "Generating documentation with Sphinx")

# Nice named target so we can run the job easily
add_custom_target(Sphinx ALL DEPENDS ${SPHINX_INDEX_FILE})
