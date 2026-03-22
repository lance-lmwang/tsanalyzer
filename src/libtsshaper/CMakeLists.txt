cmake_minimum_required(VERSION 3.10)
project(libtsshaper VERSION 1.0.0 LANGUAGES C)

# Standard C11
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Optimization Flags
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-O3 -march=native -Wall -Wextra -Wno-unused-parameter)
    add_compile_options(-fPIC)
endif()

# Include directories
include_directories(include)
include_directories(src/core)
include_directories(src/platform)
include_directories(src/pacer)

# Source files
set(TSSHAPER_SOURCES
    src/core/tsshaper.c
    src/core/tstd_model.c
    src/core/statmux.c
    src/core/interleaver.c
    src/core/spsc_queue.c
    src/pacer/pacer_loop.c
    src/platform/hal_linux.c
)

# Build the Static Library
add_library(tsshaper STATIC ${TSSHAPER_SOURCES})
target_link_libraries(tsshaper pthread m)

# Option to Build Tests
option(BUILD_TESTS "Build test bench" ON)
if(BUILD_TESTS)
    add_executable(test_bench tests/test_bench.c)
    target_link_libraries(test_bench tsshaper pthread m)
endif()
