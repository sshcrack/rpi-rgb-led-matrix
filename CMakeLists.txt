cmake_minimum_required(VERSION 3.10)

project(rpi-rgb-led-matrix VERSION 0.1.0)

include(GNUInstallDirs)

option(ENABLE_EMULATOR "Enable LED matrix emulator functionality" OFF)

set(MATRIX_CORE_SOURCES
    lib/gpio.cc
    lib/led-matrix.cc
    lib/framebuffer.cc
    lib/thread.cc
    lib/pixel-mapper.cc
    lib/multiplex-mappers.cc
    lib/hardware-mapping.c
    lib/content-streamer.cc
    lib/bdf-font.cc
    lib/graphics.cc
    lib/options-initialize.cc
    lib/led-matrix-c.cc
    lib/matrix-factory.cc
)

set(MATRIX_SOURCES ${MATRIX_CORE_SOURCES})

if(ENABLE_EMULATOR)
    list(APPEND MATRIX_SOURCES lib/emulator.cc)
    message(STATUS "Building with emulator support")
else()
    message(STATUS "Building without emulator support")
endif()

add_library(rpi-rgb-led-matrix ${MATRIX_SOURCES})

if (BUILD_SHARED_LIBS AND MSVC)
    target_compile_definitions(rpi-rgb-led-matrix PRIVATE RPI_RGB_LED_MATRIX_EXPORTS)
endif()

# Export ENABLE_EMULATOR define to consumers of the library
if(ENABLE_EMULATOR)
    target_compile_definitions(rpi-rgb-led-matrix PUBLIC ENABLE_EMULATOR)
    find_package(SDL2 CONFIG REQUIRED)
    target_link_libraries(rpi-rgb-led-matrix PRIVATE
        $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
        $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
    )
endif()

# Include directories
# Public: for consumers; Build: for internal headers
# Assume public headers are in include/ (adjust if needed)
target_include_directories(rpi-rgb-led-matrix PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
)

# Install the library and its headers
install(TARGETS rpi-rgb-led-matrix
        EXPORT rpi-rgb-led-matrix_targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

# Install all headers from include/ (adjust if needed)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Generate and install *-targets.cmake 
install(EXPORT rpi-rgb-led-matrix_targets
        FILE rpi-rgb-led-matrix-targets.cmake
        NAMESPACE rpi_rgb_led_matrix::
        DESTINATION share/rpi-rgb-led-matrix)

# Use CMakePackageConfigHelpers for proper config file generation
include(CMakePackageConfigHelpers)

# Create the config file template
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/rpi-rgb-led-matrix-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/rpi-rgb-led-matrix-config.cmake"
    INSTALL_DESTINATION share/rpi-rgb-led-matrix
)

# Install the generated config file
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rpi-rgb-led-matrix-config.cmake"
        DESTINATION share/rpi-rgb-led-matrix)