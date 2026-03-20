cmake_minimum_required(VERSION 3.10)
project(libtsshaper C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -Wall -Wextra -D_GNU_SOURCE")

# Include paths
include_directories(
    include
    ../../include
    src/core
)

# Source files
set(LIBTSSHAPER_SOURCES
    src/core/tsshaper.c
    src/core/tstd_model.c
    src/core/statmux.c
    src/core/interleaver.c
    src/platform/hal_linux.c
    src/pacer/pacer_loop.c
    ../spsc_queue.c
)

# Shared Library
add_library(tsshaper SHARED ${LIBTSSHAPER_SOURCES})
target_link_libraries(tsshaper pthread m)

# Unit Test
add_executable(test_bench tests/test_bench.c)
target_link_libraries(test_bench tsshaper pthread)

# Set runtime path for test_bench to find libtsshaper.so
set_target_properties(test_bench PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE)
set_target_properties(test_bench PROPERTIES INSTALL_RPATH ".")
set_target_properties(test_bench PROPERTIES LINK_FLAGS "-Wl,-rpath,.")
