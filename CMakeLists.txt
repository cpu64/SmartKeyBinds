cmake_minimum_required(VERSION 3.20)

project(SmartKeyBinds
    VERSION 0.1.0
    DESCRIPTION "Linux desktop macro and automation system"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# set(CMAKE_CXX_CLANG_TIDY
#     clang-tidy;
#     --config-file=${PROJECT_SOURCE_DIR}/.clang-tidy
# )

option(SKB_BUILD_COMMON
    "Build common library" ON
)

option(SKB_BUILD_PLUGIN
    "Build KWin plugin" ON
)

option(SKB_BUILD_ENGINE
    "Build SmartKeyBinds executable" ON
)

# Common is required by both plugin and engine.
if(SKB_BUILD_PLUGIN OR SKB_BUILD_ENGINE)
    set(SKB_BUILD_COMMON ON)
endif()

if(SKB_BUILD_COMMON)
    add_subdirectory(common)
endif()

if(SKB_BUILD_PLUGIN)
    add_subdirectory(kwin-plugin)
endif()

if(SKB_BUILD_ENGINE)
    add_subdirectory(engine)
endif()
