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
    macro_registry.cpp

    # Overlay
    overlay/OverlayManager.cpp
    overlay/image_loader.cpp
    overlay/wlr-layer-shell-protocol.c
    overlay/xdg-shell-protocol.c
    overlay/viewporter-protocol.c

    ${MACRO_SOURCES}
)

target_include_directories(smartkeybinds
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/macros
        ${CMAKE_CURRENT_SOURCE_DIR}/overlay
)

target_link_libraries(smartkeybinds
    PRIVATE
        smartkeybinds-common
        PkgConfig::WAYLAND
)

install(TARGETS smartkeybinds
    RUNTIME DESTINATION bin
)
