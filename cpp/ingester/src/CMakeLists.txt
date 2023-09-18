set(CMAKE_CXX_STANDARD 17)
set(CMAKE_VERBOSE_MAKEFILE TRUE)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

if (${MSVC})
    # https://github.com/boostorg/system/issues/32
    add_compile_definitions(HAVE_SNPRINTF)
    find_package(Boost 1.68 COMPONENTS program_options filesystem system thread REQUIRED)
else()
    # Boost >= 1.68 required: includes a needed fix https://github.com/boostorg/python/pull/218
    find_package(Boost 1.68 COMPONENTS program_options filesystem system thread REQUIRED)
    add_definitions(-D_GNU_SOURCE)
    # https://github.com/msys2/MINGW-packages/issues/9827
    add_compile_definitions(BOOST_ASIO_DISABLE_STD_ALIGNED_ALLOC)
endif()

# ===== External Dependency: glog
find_package(glog REQUIRED)
find_package(Arrow REQUIRED)

# This should work on Arrow 10.0+. Not Required though?
find_package(Parquet)
if (TARGET Parquet::parquet_shared)
  SET(PARQUET_LIB Parquet::parquet_shared)
elseif(TARGET parquet_shared)
  SET(PARQUET_LIB parquet_shared)
else()
  SET(PARQUET_LIB parquet)
endif()

if (NOT TARGET nlohmann_json::nlohmann_json)
    FetchContent_Declare(
            json
            GIT_REPOSITORY    https://github.com/nlohmann/json.git
            GIT_TAG           v3.10.4
    )
    FetchContent_MakeAvailable(json)
endif()

if (NOT TARGET httplib::httplib)
    FetchContent_Declare(
            cpp-httplib
            GIT_REPOSITORY    https://github.com/yhirose/cpp-httplib.git
            GIT_TAG           v0.14.0
    )
    FetchContent_MakeAvailable(cpp-httplib)
endif()

set(INCLUDE_DIRS
        ${ARROW_INCLUDE_DIR}
        ${PARQUET_INCLUDE_DIR}
        ${BOOST_INCLUDE_DIRS}
        "${river_SOURCE_DIR}/src"
        "${hiredis_SOURCE_DIR}"
)
set(LIBRARIES_TO_LINK
        "river"
        "fmt"
        "Boost::filesystem"
        "Boost::program_options"
        "Boost::system"
        "Boost::thread"
        "glog::glog"
        "hiredis"
        "nlohmann_json::nlohmann_json"
        arrow_shared
        ${PARQUET_LIB}
        "httplib::httplib"
)

set(INGESTER_HEADERS ingester.h ingester_threadpool.h ingester_settings.h ingester_http_server.h)
set(INGESTER_SOURCES ingester.cpp ingester_http_server.cpp)

if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    # Windows uses the "Shared Imp Lib", Linux does not
    list(APPEND LIBRARIES_TO_LINK "dl")
endif()



add_executable(ingester ingester_main.cpp ${INGESTER_HEADERS} ${INGESTER_SOURCES})
target_link_libraries(ingester PRIVATE ${LIBRARIES_TO_LINK})
target_compile_features(ingester PRIVATE cxx_std_17)
target_include_directories(ingester PRIVATE ${INCLUDE_DIRS})
set_target_properties(ingester PROPERTIES OUTPUT_NAME "river-ingester")

if(MSVC)
    set(MY_CXX_DEBUG_OPTIONS ${MY_CXX_DEBUG_OPTIONS} /W3)
    set(MY_CXX_RELEASE_OPTIONS ${MY_CXX_RELEASE_OPTIONS} /W3)
else()
    set(MY_CXX_DEBUG_OPTIONS ${MY_CXX_DEBUG_OPTIONS} "-Wall" "-Wextra" "-g" "-fno-inline" "-O0")
    set(MY_CXX_RELEASE_OPTIONS ${MY_CXX_RELEASE_OPTIONS} "-Wall" "-Wextra" "-O3")
endif()

target_compile_options(ingester PUBLIC "$<$<CONFIG:DEBUG>:${MY_CXX_DEBUG_OPTIONS}>")
target_compile_options(ingester PUBLIC "$<$<CONFIG:RELEASE>:${MY_CXX_RELEASE_OPTIONS}>")

if (RIVER_BUILD_TESTS)
  set(INGESTER_TEST_HEADERS ingester_test_utils.h)
  set(INGESTER_TEST_SOURCES ingester_test_main.cpp ingester_test.cpp ingester_threadpool_test.cpp integration_test.cpp ingester_settings_test.cpp)
  add_executable(ingester_test ${INGESTER_HEADERS} ${INGESTER_SOURCES} ${INGESTER_TEST_SOURCES} ${INGESTER_TEST_HEADERS})
  add_dependencies(ingester_test river)
  target_link_libraries(ingester_test PRIVATE ${LIBRARIES_TO_LINK} gtest gtest_main)
  target_compile_features(ingester_test PRIVATE cxx_std_17)
  target_include_directories(ingester_test PRIVATE ${INCLUDE_DIRS})
  add_test(NAME ingester_test COMMAND ingester_test)
  target_compile_options(ingester_test PUBLIC "$<$<CONFIG:DEBUG>:${MY_CXX_DEBUG_OPTIONS}>")
  target_compile_options(ingester_test PUBLIC "$<$<CONFIG:RELEASE>:${MY_CXX_RELEASE_OPTIONS}>")
endif()

if (RIVER_INSTALL)
  install(TARGETS ingester DESTINATION bin)
endif()

