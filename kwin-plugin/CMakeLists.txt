# find_package(ECM REQUIRED NO_MODULE)
# set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_MODULE_PATH})
#
# include(KDEInstallDirs)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
find_package(KWin REQUIRED COMPONENTS kwin)

add_library(smartkeybinds-kwin-plugin MODULE
    main.cpp
    bridge.cpp
)

set_target_properties(smartkeybinds-kwin-plugin PROPERTIES
    OUTPUT_NAME "SmartKeyBinds"
    PREFIX ""
    AUTOMOC ON
)

target_link_libraries(smartkeybinds-kwin-plugin
    PRIVATE
        smartkeybinds-common
        Qt6::Core
        KWin::kwin
)

include(KDEInstallDirs)

install(TARGETS smartkeybinds-kwin-plugin
    LIBRARY DESTINATION ${KDE_INSTALL_PLUGINDIR}/kwin/plugins
)

# Arch Linux:
# pacman -Sy vulkan-headers kwin extra-cmake-modules
#
# Docker:
# archlinux:base-devel
