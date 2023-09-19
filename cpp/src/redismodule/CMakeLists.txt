
# ===== External Dependencies
include(FetchContent)
FetchContent_Declare(
        RedisModulesSDK
        GIT_REPOSITORY    https://github.com/RedisLabsModules/RedisModulesSDK.git
        GIT_TAG           7ba899d7b75448e527bcb6f40b1091c5346eac8a
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
)

FetchContent_Declare(
        redis
        GIT_REPOSITORY    https://github.com/redis/redis.git
        GIT_TAG           ${RIVER_BUILD_REDIS_MODULE_SERVER_VERSION}
        GIT_SHALLOW 1
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
)

FetchContent_MakeAvailable(RedisModulesSDK redis)


# So that we don't include the entire redis headers; we just need to icnlude a single file
configure_file(${redis_SOURCE_DIR}/src/redismodule.h ${CMAKE_CURRENT_BINARY_DIR}/include/redismodule.h)
add_library(river_redismodule_rmutil
        STATIC
        "${redismodulessdk_SOURCE_DIR}/rmutil/util.c"
        "${redismodulessdk_SOURCE_DIR}/rmutil/strings.c"
        "${redismodulessdk_SOURCE_DIR}/rmutil/sds.c"
        "${redismodulessdk_SOURCE_DIR}/rmutil/vector.c"
        "${redismodulessdk_SOURCE_DIR}/rmutil/alloc.c"
        "${redismodulessdk_SOURCE_DIR}/rmutil/periodic.c"
        )
target_include_directories(river_redismodule_rmutil
        PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/include"
        "${redismodulessdk_SOURCE_DIR}/rmutil"
)

add_library(river_redismodule MODULE
        river_redismodule.c
        )
add_dependencies(river_redismodule river_redismodule_rmutil)
target_link_libraries(river_redismodule PRIVATE river_redismodule_rmutil)
target_include_directories(
        river_redismodule
        PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/include"
        "${redismodulessdk_SOURCE_DIR}"
)

# From https://redis.com/community/redis-modules-hub/how-to-build/
if(LINUX)
    target_compile_options(river_redismodule PRIVATE "-fPIC" "-std=gnu99")
    target_link_options(river_redismodule PRIVATE "-shared" "-Bsymbolic" "-lc")
elseif(APPLE)
    target_compile_options(river_redismodule PRIVATE "-dynamic" "-fno-common" "-std=gnu99")
    target_link_options(river_redismodule PRIVATE "-bundle" "-undefined" "dynamic_lookup" "-lc")
endif()

install(TARGETS river_redismodule LIBRARY DESTINATION lib)

