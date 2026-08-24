cmake_minimum_required(VERSION 3.10)

project(rpi-rgb-led-matrix VERSION 0.1.0)

include(GNUInstallDirs)

option(ENABLE_EMULATOR "Enable LED matrix emulator functionality" OFF)

set(MATRIX_HARDWARE_SOURCES
    lib/gpio.cc
    lib/led-matrix.cc
    lib/framebuffer.cc
    lib/thread.cc
    lib/pixel-mapper.cc
    lib/multiplex-mappers.cc
    lib/hardware-mapping.c
    lib/options-initialize.cc
    lib/led-matrix-c.cc
    lib/matrix-factory.cc
)

set(MATRIX_PORTABLE_SOURCES
    lib/frame-canvas.cc
    lib/content-streamer.cc
    lib/bdf-font.cc
    lib/graphics.cc
)

if(WIN32)
    if(NOT ENABLE_EMULATOR)
        message(FATAL_ERROR "Native Windows builds support the emulator backend only; configure with -DENABLE_EMULATOR=ON")
    endif()
    set(MATRIX_SOURCES ${MATRIX_PORTABLE_SOURCES} lib/emulator-headless.cc)
    message(STATUS "Building portable headless emulator support for Windows")
else()
    set(MATRIX_SOURCES ${MATRIX_HARDWARE_SOURCES} ${MATRIX_PORTABLE_SOURCES})
    if(ENABLE_EMULATOR)
        list(APPEND MATRIX_SOURCES lib/emulator.cc)
        message(STATUS "Building with SDL emulator support")
    else()
        message(STATUS "Building without emulator support")
    endif()
endif()

add_library(rpi-rgb-led-matrix SHARED ${MATRIX_SOURCES})

if(MSVC)
    target_compile_definitions(rpi-rgb-led-matrix PRIVATE RPI_RGB_LED_MATRIX_EXPORTS)
    set_target_properties(rpi-rgb-led-matrix PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

set(RPI_RGB_LED_MATRIX_NEEDS_SDL OFF)
if(ENABLE_EMULATOR)
    target_compile_definitions(rpi-rgb-led-matrix PUBLIC ENABLE_EMULATOR)
    if(NOT WIN32)
        set(RPI_RGB_LED_MATRIX_NEEDS_SDL ON)
        find_package(SDL2 CONFIG REQUIRED)
        target_link_libraries(rpi-rgb-led-matrix PRIVATE
            $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
            $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
        )
    endif()
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

option(BUILD_TESTS "Build memory leak test programs" OFF)

if(ENABLE_EMULATOR AND BUILD_TESTS)
    enable_testing()
    add_executable(emulator-smoke-test examples/emulator-smoke-test.cc)
    target_link_libraries(emulator-smoke-test PRIVATE rpi-rgb-led-matrix)
    add_test(NAME emulator-smoke-test COMMAND emulator-smoke-test)
    if(NOT WIN32)
        add_executable(emulator-memleak-test examples/emulator-memleak-test.cc)
        target_link_libraries(emulator-memleak-test PRIVATE rpi-rgb-led-matrix)
    endif()
endif()

# Install the library and its headers
install(TARGETS rpi-rgb-led-matrix
        EXPORT rpi-rgb-led-matrix_targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

# Install all headers from include/ (adjust if needed)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Install fonts directory for use by dependents
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/fonts/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/rpi-rgb-led-matrix/fonts)

# Generate and install *-targets.cmake 
install(EXPORT rpi-rgb-led-matrix_targets
        FILE rpi-rgb-led-matrix-targets.cmake
        NAMESPACE rpi_rgb_led_matrix::
        DESTINATION share/rpi-rgb-led-matrix)

# Use CMakePackageConfigHelpers for proper config file generation
include(CMakePackageConfigHelpers)

# Set variables for the config file
set(CMAKE_INSTALL_DATADIR_CONFIG ${CMAKE_INSTALL_DATADIR})

# Create the config file template
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/rpi-rgb-led-matrix-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/rpi-rgb-led-matrix-config.cmake"
    INSTALL_DESTINATION share/rpi-rgb-led-matrix
    PATH_VARS CMAKE_INSTALL_DATADIR_CONFIG
)

# Install the generated config file
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rpi-rgb-led-matrix-config.cmake"
        DESTINATION share/rpi-rgb-led-matrix)
