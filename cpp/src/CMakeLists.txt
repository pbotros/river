set(CMAKE_CXX_STANDARD 17)
set(CMAKE_VERBOSE_MAKEFILE TRUE)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

if (${MSVC})
    set(ADDITIONAL_LIBRARIES)
else()
    set(ADDITIONAL_LIBRARIES "dl")
    add_definitions(-D_GNU_SOURCE)
endif()

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
FetchContent_Declare(
        tracy
        GIT_REPOSITORY    https://github.com/wolfpld/tracy.git
        GIT_TAG           v0.9.1
        GIT_SHALLOW TRUE
)

set(FMT_INSTALL OFF)
set(TRACY_NO_EXIT ON)
set(TRACY_ENABLE ON)
set(TRACY_NO_BROADCAST ON)
FetchContent_MakeAvailable(fmt hiredis json cxxopts tracy)

# ===== External Dependency: glog
find_package(glog REQUIRED)

set(RIVER_HEADERS_PUBLIC
        writer.h
        river.h
        reader.h
        redis.h
        schema.h
        compression/compressor_types.h
)
set(RIVER_HEADERS_ALL
        ${RIVER_HEADERS_PUBLIC}
        redis_writer_commands.h
        compression/compressor.h)
set(RIVER_SOURCES
        writer.cpp
        reader.cpp
        redis.cpp
        schema.cpp
        compression/compressor.cpp
        redis_writer_commands.cpp)

if (RIVER_BUILD_ZFP)
    find_package(OpenMP QUIET OPTIONAL_COMPONENTS C)
    if(NOT OpenMP_C_FLAGS AND APPLE)
        # https://stackoverflow.com/questions/46414660/macos-cmake-and-openmp
        # OpenMP and Mac / Clang don't mesh well. In particular, there's no "C" components
        # installed in the Mac OpenMP conda package, so we override it to particular values
        # here.
        if(CMAKE_C_COMPILER_ID MATCHES "Clang\$")
            set(OpenMP_C_FLAGS "-Xpreprocessor -fopenmp")
            set(OpenMP_C_LIB_NAMES "omp")
            set(OpenMP_omp_LIBRARY omp)
        endif()

        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang\$")
            set(OpenMP_CXX_FLAGS "-Xpreprocessor -fopenmp")
            set(OpenMP_CXX_LIB_NAMES "omp")
            set(OpenMP_omp_LIBRARY omp)
        endif()
    endif()
    find_package(zfp REQUIRED)
    list(APPEND ADDITIONAL_LIBRARIES "zfp::zfp")
    list(APPEND RIVER_SOURCES compression/zfp_compressor.cpp)
else()
    list(APPEND RIVER_SOURCES compression/zfp_compressor_noop.cpp)
endif()

set(LIBRARIES_TO_LINK
        "fmt::fmt-header-only"
        "hiredis"
        "glog::glog"
        "nlohmann_json::nlohmann_json"
        "cxxopts"
        "TracyClient"
        ${ADDITIONAL_LIBRARIES})
set(LIBRARIES_TO_LINK_PKGCONFIG_OPTIONS "-lglog -lhiredis")


add_library(river SHARED ${RIVER_HEADERS_ALL} ${RIVER_SOURCES})
target_compile_features(river PRIVATE cxx_std_17)
target_link_libraries(river PRIVATE ${LIBRARIES_TO_LINK})
target_link_libraries(river PUBLIC hiredis)

# Make headers public
# set_target_properties(river PROPERTIES PUBLIC_HEADER "${RIVER_HEADERS_PUBLIC}")
target_include_directories(river
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  # Need to explicitly reference include/hiredis since when explicitly including
  # hiredis in a CMake project, its includes are at the top-level, but then when
  # hiredis is installed, it installs them in include/hiredis.
  PUBLIC $<INSTALL_INTERFACE:include> $<INSTALL_INTERFACE:include/hiredis>
)

add_executable(river_benchmark tools/river_benchmark.cpp)
add_dependencies(river_benchmark river)
target_compile_features(river_benchmark PRIVATE cxx_std_17)
target_link_libraries(river_benchmark PRIVATE river ${LIBRARIES_TO_LINK})

add_executable(river_writer tools/river_writer.cpp)
add_dependencies(river_writer river)
target_compile_features(river_writer PRIVATE cxx_std_17)
target_link_libraries(river_writer PRIVATE river ${LIBRARIES_TO_LINK})

add_executable(river_reader tools/river_reader.cpp)
add_dependencies(river_reader river)
target_compile_features(river_reader PRIVATE cxx_std_17)
target_link_libraries(river_reader PRIVATE river ${LIBRARIES_TO_LINK})

add_executable(river_example tools/river_example.cpp)
add_dependencies(river_example river)
target_compile_features(river_example PRIVATE cxx_std_17)
target_link_libraries(river_example PRIVATE river ${LIBRARIES_TO_LINK})


set(MY_CXX_DEBUG_OPTIONS "-Wall" "-g" "-fno-inline" "-O0")
set(MY_CXX_RELEASE_OPTIONS "-Wall" "-O3")

if(MSVC)
    set(MY_CXX_DEBUG_OPTIONS ${MY_CXX_DEBUG_OPTIONS} /W3)
    set(MY_CXX_RELEASE_OPTIONS ${MY_CXX_RELEASE_OPTIONS} /W3)
    target_compile_definitions(river PUBLIC _WINSOCKAPI_)
    target_link_libraries(river PRIVATE ws2_32)
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

  if (${CMAKE_VERSION} VERSION_LESS "3.23")
      # From https://stackoverflow.com/questions/48212771/cmake-install-header-files-and-maintain-directory-hierarchy
      # FILE_SET doesn't exist here
      INSTALL(
              TARGETS river
              EXPORT ${targets_export_name}
              LIBRARY DESTINATION lib)

      macro(install_with_directory)
          set(optionsArgs "")
          set(oneValueArgs "DESTINATION")
          set(multiValueArgs "FILES")
          cmake_parse_arguments(CAS "${optionsArgs}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN} )

          foreach(FILE ${CAS_FILES})
              get_filename_component(DIR ${FILE} DIRECTORY)
              INSTALL(
                      FILES ${FILE} DESTINATION ${CAS_DESTINATION}/${DIR})
          endforeach()
      endmacro(install_with_directory)

      install_with_directory(
          DESTINATION include/river
          FILES ${RIVER_HEADERS_PUBLIC})
    else()
      target_sources(river PUBLIC FILE_SET river_headers_public TYPE HEADERS FILES "${RIVER_HEADERS_PUBLIC}")
      INSTALL(
              TARGETS river
              EXPORT ${targets_export_name}
              LIBRARY DESTINATION lib
              FILE_SET river_headers_public DESTINATION include/river)
  endif()

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

  if (RIVER_INSTALL_BENCHMARK)
    install(TARGETS river_benchmark DESTINATION bin)
  endif()
endif()

###############
# ======== TESTS
if (RIVER_BUILD_TESTS)
  add_executable(river_test
          tests/river_test.cpp
          tests/reader_test.cpp
          tests/writer_test.cpp
          tests/redis_test.cpp
          tests/integration_test.cpp
          tests/compressor_test.cpp
  )
  add_dependencies(river_test river)
  target_compile_features(river_test PRIVATE cxx_std_17)
  target_link_libraries(river_test PRIVATE river ${LIBRARIES_TO_LINK} gtest gtest_main)
  add_test(NAME river_test COMMAND river_test)
  target_compile_options(river_test PUBLIC "$<$<CONFIG:DEBUG>:${MY_CXX_DEBUG_OPTIONS}>")
  target_compile_options(river_test PUBLIC "$<$<CONFIG:RELEASE>:${MY_CXX_RELEASE_OPTIONS}>")
endif()

if (RIVER_BUILD_REDIS_MODULE)
    add_subdirectory(redismodule)
endif()
