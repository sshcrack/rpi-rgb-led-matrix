cmake_minimum_required(VERSION 3.5.0)
project(rpi-rgb-led-matrix VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 14)

# Option to enable/disable emulator functionality
option(ENABLE_EMULATOR "Enable LED matrix emulator functionality" OFF)

# Define core library source files
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

# Set up emulator if enabled
set(MATRIX_SOURCES ${MATRIX_CORE_SOURCES})

if(ENABLE_EMULATOR)
    # Add emulator source
    list(APPEND MATRIX_SOURCES lib/emulator.cc)

    message(STATUS "Building with emulator support")
else()
    message(STATUS "Building without emulator support")
endif()

add_library(${PROJECT_NAME} SHARED ${MATRIX_SOURCES})

# Main Target Compile
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_14)

# Include directories
target_include_directories(${PROJECT_NAME}
    PRIVATE
    # where the library itself will look for its internal headers
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
    PUBLIC
    # where top-level project will look for the library's public headers
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    # where external projects will look for the library's public headers
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Export ENABLE_EMULATOR define to consumers of the library
if(ENABLE_EMULATOR)
    target_compile_definitions(${PROJECT_NAME} PUBLIC ENABLE_EMULATOR)

    # Find SDL2 for the emulator
    find_package(SDL2 REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL2)
endif()