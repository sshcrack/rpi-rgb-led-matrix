cmake_minimum_required(VERSION 3.5.0)
project(rpi-rgb-led-matrix VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)

file(GLOB_RECURSE SHARED_SOURCES
        lib/*.c*
)

add_library(${PROJECT_NAME} SHARED)
target_sources(${PROJECT_NAME}
        PRIVATE
        ${SHARED_SOURCES}
)
# Main Target Compile

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
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
