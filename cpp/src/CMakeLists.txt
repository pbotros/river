set(CMAKE_CXX_STANDARD 14)
set(CMAKE_VERBOSE_MAKEFILE TRUE)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# ===== External Dependencies
include(FetchContent)

FetchContent_Declare(
  fmt
  GIT_REPOSITORY    https://github.com/fmtlib/fmt.git
  GIT_TAG           6.2.0
)
FetchContent_Declare(
  hiredis
  GIT_REPOSITORY    https://github.com/redis/hiredis.git
  GIT_TAG           b484021859c2b395f13163caaa9b877b5284ab07
)
FetchContent_Declare(
  json
  GIT_REPOSITORY    https://github.com/nlohmann/json.git
  GIT_TAG           v3.10.4
)
FetchContent_Declare(
  cxxopts
  GIT_REPOSITORY    https://github.com/jarro2783/cxxopts.git
  GIT_TAG           v3.0.0
)

FetchContent_MakeAvailable(fmt hiredis json cxxopts)

# ===== External Dependency: glog
find_package(glog REQUIRED)

if (${MSVC})
    set(ADDITIONAL_LIBRARIES)
else()
    set(ADDITIONAL_LIBRARIES "dl")
    add_definitions(-D_GNU_SOURCE)
endif()

set(LIBRARIES_TO_LINK
        "fmt"
        "hiredis"
        "glog::glog"
        "nlohmann_json::nlohmann_json"
        "cxxopts"
        ${ADDITIONAL_LIBRARIES})
set(LIBRARIES_TO_LINK_PKGCONFIG_OPTIONS "-lglog -lhiredis")

set(RIVER_HEADERS writer.h river.h reader.h redis.h schema.h)
set(RIVER_SOURCES writer.cpp reader.cpp redis.cpp schema.cpp)

add_library(river SHARED ${RIVER_HEADERS} ${RIVER_SOURCES})
target_compile_features(river PRIVATE cxx_std_14)
target_link_libraries(river PRIVATE ${LIBRARIES_TO_LINK})
target_link_libraries(river PUBLIC hiredis)

# Make headers public
set_target_properties(river PROPERTIES PUBLIC_HEADER "${RIVER_HEADERS}")
target_include_directories(river
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  # Need to explicitly reference include/hiredis since when explicitly including
  # hiredis in a CMake project, its includes are at the top-level, but then when
  # hiredis is installed, it installs them in include/hiredis.
  PUBLIC $<INSTALL_INTERFACE:include> $<INSTALL_INTERFACE:include/hiredis>
)



add_executable(river_benchmark tools/river_benchmark.cpp)
add_dependencies(river_benchmark river)
target_compile_features(river_benchmark PRIVATE cxx_std_14)
target_link_libraries(river_benchmark PRIVATE river ${LIBRARIES_TO_LINK})

add_executable(river_writer tools/river_writer.cpp)
add_dependencies(river_writer river)
target_compile_features(river_writer PRIVATE cxx_std_14)
target_link_libraries(river_writer PRIVATE river ${LIBRARIES_TO_LINK})

add_executable(river_reader tools/river_reader.cpp)
add_dependencies(river_reader river)
target_compile_features(river_reader PRIVATE cxx_std_14)
target_link_libraries(river_reader PRIVATE river ${LIBRARIES_TO_LINK})

add_executable(river_example tools/river_example.cpp)
add_dependencies(river_example river)
target_compile_features(river_example PRIVATE cxx_std_14)
target_link_libraries(river_example PRIVATE river ${LIBRARIES_TO_LINK})


set(MY_CXX_DEBUG_OPTIONS "-Wall" "-g" "-fno-inline" "-O0")
set(MY_CXX_RELEASE_OPTIONS "-Wall" "-O3")

if(MSVC)
    set(MY_CXX_DEBUG_OPTIONS ${MY_CXX_DEBUG_OPTIONS} /W3)
    set(MY_CXX_RELEASE_OPTIONS ${MY_CXX_RELEASE_OPTIONS} /W3)
else()
    set(MY_CXX_DEBUG_OPTIONS ${MY_CXX_DEBUG_OPTIONS}  -Wall -Wextra -pedantic)
    set(MY_CXX_RELEASE_OPTIONS ${MY_CXX_RELEASE_OPTIONS} -Wall -Wextra -pedantic)
endif()

target_compile_options(river PUBLIC "$<$<CONFIG:DEBUG>:${MY_CXX_DEBUG_OPTIONS}>")
target_compile_options(river PUBLIC "$<$<CONFIG:RELEASE>:${MY_CXX_RELEASE_OPTIONS}>")
target_compile_options(river_benchmark PUBLIC "$<$<CONFIG:DEBUG>:${MY_CXX_DEBUG_OPTIONS}>")
target_compile_options(river_benchmark PUBLIC "$<$<CONFIG:RELEASE>:${MY_CXX_RELEASE_OPTIONS}>")

################
### Installs ###
if (RIVER_INSTALL)
  set(targets_export_name river-targets)
  install(TARGETS river
    EXPORT ${targets_export_name}
    LIBRARY DESTINATION lib
    PUBLIC_HEADER DESTINATION include/river)
  install(TARGETS hiredis DESTINATION lib)

  #########################################
  # Handle .cmake and pkg-config .pc files
  # Below copied from fmt and from glog
  ########################################
  set(version_config ${PROJECT_BINARY_DIR}/river-config-version.cmake)
  set(project_config ${PROJECT_BINARY_DIR}/river-config.cmake)
  set(pkgconfig ${PROJECT_BINARY_DIR}/river.pc)

  # Generate the version, config and target files into the build directory.
  write_basic_package_version_file(
    ${version_config}
    VERSION ${PACKAGE_VERSION}
    COMPATIBILITY AnyNewerVersion)

  get_filename_component(libdir_for_pc_file ${CMAKE_INSTALL_LIBDIR}/${exec_prefix} ABSOLUTE)
  get_filename_component(includedir_for_pc_file ${CMAKE_INSTALL_INCLUDEDIR}/${prefix} ABSOLUTE)

  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/river.pc.in"
    "${pkgconfig}"
    @ONLY)
  configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/river-config.cmake.in
    ${project_config}
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/river)

  export(TARGETS ${INSTALL_TARGETS} NAMESPACE river::
    FILE ${PROJECT_BINARY_DIR}/${targets_export_name}.cmake)

  export (PACKAGE river)
  get_filename_component (_PREFIX "${CMAKE_INSTALL_PREFIX}" ABSOLUTE)

  # Directory containing the find modules relative to the config install
  # directory.
  file (RELATIVE_PATH river_REL_CMake_MODULES
    ${_PREFIX}/${_river_CMake_INSTALLDIR}
    ${_PREFIX}/${_river_CMake_DATADIR}/river-modules.cmake)

  get_filename_component (river_REL_CMake_DATADIR ${river_REL_CMake_MODULES} DIRECTORY)

  set (river_FULL_CMake_DATADIR
    ${CMAKE_CURRENT_BINARY_DIR}/${_river_CMake_DATADIR})

  configure_file (river-modules.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/river-modules.cmake @ONLY)

  # Install version, config and target files.
  install(
    FILES ${project_config} ${version_config} ${CMAKE_CURRENT_BINARY_DIR}/river-modules.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/river)
  install(EXPORT ${targets_export_name} DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/river NAMESPACE river::)

  install(FILES "${pkgconfig}" DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
endif()

###############
# ======== TESTS
if (RIVER_BUILD_TESTS)
  add_executable(river_test tests/river_test.cpp tests/reader_test.cpp tests/writer_test.cpp tests/redis_test.cpp tests/integration_test.cpp)
  add_dependencies(river_test river)
  target_compile_features(river_test PRIVATE cxx_std_14)
  target_link_libraries(river_test PRIVATE river ${LIBRARIES_TO_LINK} gtest gtest_main)
  add_test(NAME river_test COMMAND river_test)
  target_compile_options(river_test PUBLIC "$<$<CONFIG:DEBUG>:${MY_CXX_DEBUG_OPTIONS}>")
  target_compile_options(river_test PUBLIC "$<$<CONFIG:RELEASE>:${MY_CXX_RELEASE_OPTIONS}>")
endif()
