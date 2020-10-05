set(CMAKE_CXX_STANDARD 14)
set(CMAKE_VERBOSE_MAKEFILE TRUE)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# ===== External Dependencies
include(FetchContent)

set(JSON_Install OFF)
set(JSON_BuildTests OFF)
set(FMT_INSTALL OFF)
set(INSTALL_GTEST OFF)
set(CXXOPTS_BUILD_TESTS OFF)
set(CXXOPTS_BUILD_EXAMPLES OFF)
set(CXXOPTS_ENABLE_INSTALL OFF)
set(DISABLE_TESTS ON)  # hiredis
set(FMT_DEBUG_POSTFIX "")

FetchContent_Declare(
  googletest
  GIT_REPOSITORY    https://github.com/google/googletest.git
  GIT_TAG           master
)
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
  hiredis
  GIT_REPOSITORY    https://github.com/redis/hiredis.git
  GIT_TAG           b484021859c2b395f13163caaa9b877b5284ab07
)
FetchContent_Declare(
  json
  GIT_REPOSITORY    https://github.com/nlohmann/json.git
  GIT_TAG           v3.9.1
)
FetchContent_Declare(
  cxxopts
  GIT_REPOSITORY    https://github.com/jarro2783/cxxopts.git
  GIT_TAG           v2.2.1
)


FetchContent_MakeAvailable(googletest fmt hiredis json cxxopts)

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

set(RIVER_HEADERS writer.h river.h reader.h redis.h schema.h)
set(RIVER_SOURCES writer.cpp reader.cpp redis.cpp schema.cpp)

add_library(river SHARED ${RIVER_HEADERS} ${RIVER_SOURCES})
target_compile_features(river PRIVATE cxx_std_14)
target_include_directories(river SYSTEM PUBLIC ${INCLUDE_DIRS})
target_link_libraries(river PRIVATE ${LIBRARIES_TO_LINK})


add_executable(river_test tests/river_test.cpp tests/reader_test.cpp tests/writer_test.cpp tests/redis_test.cpp tests/integration_test.cpp)
add_dependencies(river_test river)
target_compile_features(river_test PRIVATE cxx_std_14)
target_link_libraries(river_test river ${LIBRARIES_TO_LINK} gtest gtest_main)
add_test(NAME river_test COMMAND river_test)

add_executable(river_benchmark tools/river_benchmark.cpp)
target_compile_features(river_benchmark PRIVATE cxx_std_14)
target_link_libraries(river_benchmark river ${LIBRARIES_TO_LINK})

add_executable(river_writer tools/river_writer.cpp)
target_compile_features(river_writer PRIVATE cxx_std_14)
target_link_libraries(river_writer river ${LIBRARIES_TO_LINK})

add_executable(river_reader tools/river_reader.cpp)
target_compile_features(river_reader PRIVATE cxx_std_14)
target_link_libraries(river_reader river ${LIBRARIES_TO_LINK})

add_executable(river_example tools/river_example.cpp)
target_compile_features(river_example PRIVATE cxx_std_14)
target_link_libraries(river_example river ${LIBRARIES_TO_LINK})


set(MY_CXX_DEBUG_OPTIONS "-Wall" "-g" "-fno-inline" "-O0")
set(MY_CXX_RELEASE_OPTIONS "-Wall" "-O3")

if(MSVC)
    set(MY_CXX_DEBUG_OPTIONS ${MY_CXX_DEBUG_OPTIONS} /W4)
    set(MY_CXX_RELEASE_OPTIONS ${MY_CXX_RELEASE_OPTIONS} /W4)
else()
    set(MY_CXX_DEBUG_OPTIONS ${MY_CXX_DEBUG_OPTIONS}  -Wall -Wextra -pedantic)
    set(MY_CXX_RELEASE_OPTIONS ${MY_CXX_RELEASE_OPTIONS} -Wall -Wextra -pedantic)
endif()

target_compile_options(river PUBLIC "$<$<CONFIG:DEBUG>:${MY_CXX_DEBUG_OPTIONS}>")
target_compile_options(river PUBLIC "$<$<CONFIG:RELEASE>:${MY_CXX_RELEASE_OPTIONS}>")
target_compile_options(river_test PUBLIC "$<$<CONFIG:DEBUG>:${MY_CXX_DEBUG_OPTIONS}>")
target_compile_options(river_test PUBLIC "$<$<CONFIG:RELEASE>:${MY_CXX_RELEASE_OPTIONS}>")
target_compile_options(river_benchmark PUBLIC "$<$<CONFIG:DEBUG>:${MY_CXX_DEBUG_OPTIONS}>")
target_compile_options(river_benchmark PUBLIC "$<$<CONFIG:RELEASE>:${MY_CXX_RELEASE_OPTIONS}>")

install(TARGETS river DESTINATION lib)
install(TARGETS hiredis DESTINATION lib)
install(FILES ${RIVER_HEADERS} DESTINATION include/river)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/to_copy)
add_custom_command(TARGET river POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:river> ${CMAKE_CURRENT_BINARY_DIR}/to_copy/$<TARGET_FILE_NAME:river>
)
add_custom_command(TARGET river POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:hiredis> ${CMAKE_CURRENT_BINARY_DIR}/to_copy/$<TARGET_FILE_NAME:hiredis>
)
