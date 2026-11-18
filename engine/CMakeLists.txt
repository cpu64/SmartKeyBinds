find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client)

file(GLOB_RECURSE MACRO_SOURCES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/macros/*.cpp"
)

add_executable(smartkeybinds
    main.cpp
    fake_input.cpp
    input_listener.cpp
    context.cpp

    # Overlay
    OverlayManager.cpp
    image_loader.cpp

    ${PROJECT_SOURCE_DIR}/external/wayland/wlr-layer-shell-protocol.c
    ${PROJECT_SOURCE_DIR}/external/wayland/xdg-shell-protocol.c
    ${PROJECT_SOURCE_DIR}/external/wayland/viewporter-protocol.c

    ${MACRO_SOURCES}
)

target_include_directories(smartkeybinds
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/macros
        ${PROJECT_SOURCE_DIR}/external/wayland
        ${PROJECT_SOURCE_DIR}/external/image
)

target_link_libraries(smartkeybinds
    PRIVATE
        smartkeybinds-common
        PkgConfig::WAYLAND
)

install(TARGETS smartkeybinds
    RUNTIME DESTINATION bin
)
