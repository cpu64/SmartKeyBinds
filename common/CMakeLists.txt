find_package(Threads REQUIRED)

add_library(smartkeybinds-common STATIC
    ipc/client.cpp
    ipc/server.cpp
    protocol/protocol.cpp
    logger/logger.cpp
    logger/sinks.cpp
)

target_include_directories(smartkeybinds-common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(smartkeybinds-common
    PRIVATE
        Threads::Threads
)

set_target_properties(smartkeybinds-common PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
