cmake_minimum_required(VERSION 3.27)
project(focusZ DESCRIPTION "Z-Axis Depth Focus Layout plugin for Hyprland" VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Collect sources
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/*.hpp")

# Build as shared library
add_library(focusZ SHARED ${SOURCES})

# Find dependencies via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(HYPRLAND REQUIRED IMPORTED_TARGET hyprland)

# Link
target_link_libraries(focusZ PRIVATE PkgConfig::HYPRLAND rt)

# Include paths
target_include_directories(focusZ PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Ensure symbols are exported
set_target_properties(focusZ PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# Install
include(GNUInstallDirs)
install(TARGETS focusZ LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
