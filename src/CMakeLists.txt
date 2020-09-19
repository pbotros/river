set(CMAKE_CXX_STANDARD 14)
set(CMAKE_VERBOSE_MAKEFILE TRUE)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

function(import_external_project filename target)
    if (NOT TARGET ${target})
        configure_file(${filename} ${target}-download/CMakeLists.txt)
        execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
                RESULT_VARIABLE result
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${target}-download )
        if(result)
            message(FATAL_ERROR "CMake step for ${target} failed: ${result}")
        endif()
        execute_process(COMMAND ${CMAKE_COMMAND} --build .
                RESULT_VARIABLE result
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${target}-download )
        if(result)
            message(FATAL_ERROR "Build step for ${target} failed: ${result}")
        endif()
        add_subdirectory(${CMAKE_CURRENT_BINARY_DIR}/${target}-src
                ${CMAKE_CURRENT_BINARY_DIR}/${target}-build
                EXCLUDE_FROM_ALL)
    endif()
endfunction()

# ===== External Dependency: GoogleTest
import_external_project(CMakeLists.txt.in.googletest googletest)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
if (CMAKE_VERSION VERSION_LESS 2.8.11)
    include_directories("${gtest_SOURCE_DIR}/include")
endif()

# ===== External Dependency: fmt
import_external_project(CMakeLists.txt.in.fmt fmt)

# ===== External Dependency: hiredis
import_external_project(CMakeLists.txt.in.hiredis hiredis)

# ===== External Dependency: json
import_external_project(CMakeLists.txt.in.json json)

# ===== External Dependency: cxxopts
import_external_project(CMakeLists.txt.in.cxxopts cxxopts)

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
target_link_libraries(river ${LIBRARIES_TO_LINK})


add_executable(river_test tests/river_test.cpp tests/reader_test.cpp tests/writer_test.cpp tests/redis_test.cpp tests/integration_test.cpp)
add_dependencies(river_test river)
target_compile_features(river_test PRIVATE cxx_std_14)
target_include_directories(river_test SYSTEM PUBLIC ${INCLUDE_DIRS})
target_link_libraries(river_test river ${LIBRARIES_TO_LINK} gtest gtest_main)
add_test(NAME river_test COMMAND river_test)

add_executable(river_benchmark tools/river_benchmark.cpp)
add_dependencies(river_benchmark river)
target_compile_features(river_benchmark PRIVATE cxx_std_14)
target_include_directories(river_benchmark PUBLIC ${INCLUDE_DIRS})
target_link_libraries(river_benchmark river ${LIBRARIES_TO_LINK})

add_executable(river_writer tools/river_writer.cpp)
add_dependencies(river_writer river)
target_compile_features(river_writer PRIVATE cxx_std_14)
target_include_directories(river_writer PUBLIC ${INCLUDE_DIRS})
target_link_libraries(river_writer river ${LIBRARIES_TO_LINK})

add_executable(river_reader tools/river_reader.cpp)
add_dependencies(river_reader river)
target_compile_features(river_reader PRIVATE cxx_std_14)
target_include_directories(river_reader PUBLIC ${INCLUDE_DIRS})
target_link_libraries(river_reader river ${LIBRARIES_TO_LINK})

add_executable(river_example tools/river_example.cpp)
add_dependencies(river_example river)
target_compile_features(river_example PRIVATE cxx_std_14)
target_include_directories(river_example PUBLIC ${INCLUDE_DIRS})
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
