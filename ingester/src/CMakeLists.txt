set(CMAKE_CXX_STANDARD 14)
set(CMAKE_VERBOSE_MAKEFILE TRUE)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

find_package(Python3 COMPONENTS Interpreter Development)
find_package(PythonLibs 3.7 REQUIRED)

if (${MSVC})
    # https://github.com/boostorg/system/issues/32
    add_compile_definitions(HAVE_SNPRINTF)
    find_package(Boost 1.68 COMPONENTS program_options filesystem system thread REQUIRED)
else()
    # Boost >= 1.68 required: includes a needed fix https://github.com/boostorg/python/pull/218
    find_package(Boost 1.68 COMPONENTS program_options filesystem system thread REQUIRED)
    add_definitions(-D_GNU_SOURCE)
endif()

# ===== External Dependency: glog
find_package(glog REQUIRED)

find_package(Arrow REQUIRED)
add_library(arrow SHARED IMPORTED)
set_target_properties(arrow PROPERTIES IMPORTED_LOCATION ${ARROW_SHARED_LIB})

find_package(Parquet REQUIRED)
add_library(parquet SHARED IMPORTED)
set_target_properties(parquet PROPERTIES IMPORTED_LOCATION ${PARQUET_SHARED_LIB})

if (${MSVC})
    # Windows uses the "Shared Imp Lib"
    set(ARROW_PARQUET_LIBS ${ARROW_SHARED_IMP_LIB} ${PARQUET_SHARED_IMP_LIB})
    set(DL_LIB "")
else()
    set(ARROW_PARQUET_LIBS ${ARROW_SHARED_LIB} ${PARQUET_SHARED_LIB})
    set(DL_LIB "dl")
endif()

set(INCLUDE_DIRS ${ARROW_INCLUDE_DIR} ${PARQUET_INCLUDE_DIR} ${BOOST_INCLUDE_DIRS} "${river_SOURCE_DIR}/src" "${hiredis_SOURCE_DIR}")
set(LIBRARIES_TO_LINK
        "river"
        "fmt"
        "Boost::filesystem"
        "Boost::program_options"
        "Boost::system"
        "Boost::thread"
        "glog::glog"
        "hiredis"
        ${DL_LIB}
        ${ARROW_PARQUET_LIBS})

set(INGESTER_HEADERS ingester.h ingester_threadpool.h)
set(INGESTER_SOURCES ingester.cpp)

add_executable(ingester ingester_main.cpp ${INGESTER_HEADERS} ${INGESTER_SOURCES})
target_link_libraries(ingester PUBLIC ${LIBRARIES_TO_LINK})
target_compile_features(ingester PRIVATE cxx_std_14)
target_include_directories(ingester PUBLIC ${INCLUDE_DIRS})
set_target_properties(ingester PROPERTIES OUTPUT_NAME "river-ingester")

set(MY_CXX_DEBUG_OPTIONS "-Wall" "-Wextra" "-g" "-fno-inline" "-O0")
set(MY_CXX_RELEASE_OPTIONS "-Wall" "-Wextra" "-O3")
target_compile_options(ingester PUBLIC "$<$<CONFIG:DEBUG>:${MY_CXX_DEBUG_OPTIONS}>")
target_compile_options(ingester PUBLIC "$<$<CONFIG:RELEASE>:${MY_CXX_RELEASE_OPTIONS}>")

if (RIVER_BUILD_TESTS)
  set(INGESTER_TEST_HEADERS ingester_test_utils.h)
  set(INGESTER_TEST_SOURCES ingester_test_main.cpp ingester_test.cpp ingester_threadpool_test.cpp integration_test.cpp)
  add_executable(ingester_test ${INGESTER_HEADERS} ${INGESTER_SOURCES} ${INGESTER_TEST_SOURCES} ${INGESTER_TEST_HEADERS})
  add_dependencies(ingester_test river)
  target_link_libraries(ingester_test ${LIBRARIES_TO_LINK} gtest gtest_main)
  target_compile_features(ingester_test PRIVATE cxx_std_14)
  target_include_directories(ingester_test PUBLIC ${INCLUDE_DIRS})
  add_test(NAME ingester_test COMMAND ingester_test)
  target_compile_options(ingester_test PUBLIC "$<$<CONFIG:DEBUG>:${MY_CXX_DEBUG_OPTIONS}>")
  target_compile_options(ingester_test PUBLIC "$<$<CONFIG:RELEASE>:${MY_CXX_RELEASE_OPTIONS}>")
endif()

install(TARGETS ingester DESTINATION bin)
