cmake_minimum_required(VERSION 3.10)

project(rpi-rgb-led-matrix VERSION 0.2.1 LANGUAGES C CXX)

include(GNUInstallDirs)

option(ENABLE_EMULATOR "Enable LED matrix emulator functionality" OFF)

# The emulator core needs no dependencies. ENABLE_EMULATOR_SDL adds the SDL2
# window; by default it is on (outside Windows) when SDL2 is found.
if(ENABLE_EMULATOR AND NOT DEFINED ENABLE_EMULATOR_SDL)
    set(_rpi_rgb_led_matrix_sdl_default OFF)
    if(NOT WIN32)
        find_package(SDL2 CONFIG QUIET)
        if(SDL2_FOUND)
            set(_rpi_rgb_led_matrix_sdl_default ON)
        endif()
    endif()
    option(ENABLE_EMULATOR_SDL "Add the SDL2 window to the emulator" ${_rpi_rgb_led_matrix_sdl_default})
endif()
if(NOT ENABLE_EMULATOR)
    set(ENABLE_EMULATOR_SDL OFF)
endif()

set(MATRIX_HARDWARE_SOURCES
    lib/gpio.cc
    lib/led-matrix.cc
    lib/framebuffer.cc
    lib/thread.cc
    lib/multiplex-mappers.cc
    lib/hardware-mapping.c
    lib/options-initialize.cc
    lib/led-matrix-c.cc
    lib/matrix-factory.cc
    lib/content-streamer-c.cc
    lib/frame-sequence.cc
    lib/frame-sequence-c.cc
    lib/rp1/rp1_pio_backend.cc
    lib/rp1/rp1_pio_support.c
    lib/rp1/rp1_rio_backend.cc
)

set(MATRIX_PORTABLE_SOURCES
    lib/frame-canvas.cc
    lib/content-streamer.cc
    lib/bdf-font.cc
    lib/graphics.cc
    lib/refresh-timing.cc
    lib/pixel-mapper.cc
)

if(WIN32)
    if(NOT ENABLE_EMULATOR)
        message(FATAL_ERROR "Native Windows builds support the emulator backend only; configure with -DENABLE_EMULATOR=ON")
    endif()
    set(MATRIX_SOURCES ${MATRIX_PORTABLE_SOURCES} lib/emulator-portable-options.cc
        lib/emulator-flags.cc lib/emulator-core.cc)
    # pixel-mapper.cc is upstream code using POSIX string functions.
    if(MSVC)
        set(_rpi_rgb_led_matrix_force_include /FI${CMAKE_CURRENT_SOURCE_DIR}/lib/win32-compat.h)
    else()
        set(_rpi_rgb_led_matrix_force_include -include ${CMAKE_CURRENT_SOURCE_DIR}/lib/win32-compat.h)
    endif()
    set_source_files_properties(lib/pixel-mapper.cc PROPERTIES
        COMPILE_OPTIONS "${_rpi_rgb_led_matrix_force_include}")
else()
    set(MATRIX_SOURCES ${MATRIX_HARDWARE_SOURCES} ${MATRIX_PORTABLE_SOURCES} lib/emulator-flags.cc)
    if(ENABLE_EMULATOR)
        list(APPEND MATRIX_SOURCES lib/emulator-core.cc)
    else()
        # Same emulator API, but EmulatorMatrix::Create() returns nullptr.
        list(APPEND MATRIX_SOURCES lib/emulator-unsupported.cc)
        message(STATUS "Building without emulator support")
    endif()
endif()
if(ENABLE_EMULATOR_SDL)
    list(APPEND MATRIX_SOURCES lib/emulator-sdl.cc)
    message(STATUS "Building the emulator with the SDL window")
elseif(ENABLE_EMULATOR)
    message(STATUS "Building the headless emulator (no SDL window)")
endif()

add_library(rpi-rgb-led-matrix SHARED ${MATRIX_SOURCES})
target_compile_features(rpi-rgb-led-matrix PUBLIC cxx_std_17)

if(MSVC)
    target_compile_definitions(rpi-rgb-led-matrix PRIVATE RPI_RGB_LED_MATRIX_EXPORTS)
    set_target_properties(rpi-rgb-led-matrix PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

set(RPI_RGB_LED_MATRIX_NEEDS_SDL OFF)
if(ENABLE_EMULATOR)
    target_compile_definitions(rpi-rgb-led-matrix PUBLIC ENABLE_EMULATOR)
endif()
if(ENABLE_EMULATOR_SDL)
    set(RPI_RGB_LED_MATRIX_NEEDS_SDL ON)
    find_package(SDL2 CONFIG REQUIRED)
    target_compile_definitions(rpi-rgb-led-matrix PRIVATE ENABLE_EMULATOR_SDL)
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
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/rp1/rp1_pio_vendor/piolib/include
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/rp1/rp1_pio_vendor/include
)

option(BUILD_TESTS "Build test programs" OFF)

if(BUILD_TESTS)
    enable_testing()
    add_executable(framebuffer-regression-test examples/framebuffer-regression-test.cc)
    target_link_libraries(framebuffer-regression-test PRIVATE rpi-rgb-led-matrix)
    add_test(NAME framebuffer-regression-test COMMAND framebuffer-regression-test)
    if(NOT WIN32)
        add_executable(bulk-framebuffer-test examples/bulk-framebuffer-test.cc)
        target_link_libraries(bulk-framebuffer-test PRIVATE rpi-rgb-led-matrix)
        add_test(NAME bulk-framebuffer-test COMMAND bulk-framebuffer-test)
    endif()
    add_executable(refresh-timing-test examples/refresh-timing-test.cc)
    target_link_libraries(refresh-timing-test PRIVATE rpi-rgb-led-matrix)
    add_test(NAME refresh-timing-test COMMAND refresh-timing-test)
    add_executable(stream-io-test examples/stream-io-test.cc)
    target_link_libraries(stream-io-test PRIVATE rpi-rgb-led-matrix)
    add_test(NAME stream-io-test COMMAND stream-io-test)
    add_executable(content-streamer-test examples/content-streamer-test.cc)
    target_link_libraries(content-streamer-test PRIVATE rpi-rgb-led-matrix)
    add_test(NAME content-streamer-repeat-peek COMMAND content-streamer-test repeat-peek)
    add_test(NAME content-streamer-consume-peek COMMAND content-streamer-test consume-peek)
    if(NOT WIN32)
        # Compiled without ENABLE_EMULATOR against either library build, to
        # check the emulator headers don't depend on the define.
        add_executable(emulator-api-test examples/emulator-api-test.cc)
        target_link_libraries(emulator-api-test PRIVATE rpi-rgb-led-matrix)
        target_compile_options(emulator-api-test PRIVATE -UENABLE_EMULATOR)
        add_test(NAME emulator-api-test COMMAND emulator-api-test)
    endif()
endif()

if(ENABLE_EMULATOR AND BUILD_TESTS)
    enable_testing()
    add_executable(emulator-lifecycle-test examples/emulator-lifecycle-test.cc)
    target_link_libraries(emulator-lifecycle-test PRIVATE rpi-rgb-led-matrix)
    if(ENABLE_EMULATOR_SDL)
        # The test checks that headless emulators never initialize SDL.
        target_compile_definitions(emulator-lifecycle-test PRIVATE EMULATOR_TEST_HAS_SDL)
        target_link_libraries(emulator-lifecycle-test PRIVATE
            $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>)
    endif()
    add_test(NAME emulator-lifecycle-test COMMAND emulator-lifecycle-test)
    if(ENABLE_EMULATOR_SDL)
        add_test(NAME emulator-display-failure COMMAND emulator-lifecycle-test display-failure)
        set_tests_properties(emulator-display-failure PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    endif()
    if(NOT WIN32)
        add_executable(matrix-factory-flags-test examples/matrix-factory-flags-test.cc)
        target_link_libraries(matrix-factory-flags-test PRIVATE rpi-rgb-led-matrix)
        add_test(NAME matrix-factory-flags-test COMMAND matrix-factory-flags-test)
    endif()
    add_executable(emulator-pixel-mapper-test examples/emulator-pixel-mapper-test.cc)
    target_link_libraries(emulator-pixel-mapper-test PRIVATE rpi-rgb-led-matrix)
    add_test(NAME emulator-pixel-mapper-test COMMAND emulator-pixel-mapper-test)
    add_executable(emulator-frame-callback-test examples/emulator-frame-callback-test.cc)
    target_link_libraries(emulator-frame-callback-test PRIVATE rpi-rgb-led-matrix)
    add_test(NAME emulator-frame-callback-test COMMAND emulator-frame-callback-test)
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
