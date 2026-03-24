cmake_minimum_required(VERSION 3.10)
project(libtsshaper VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-O3 -Wall -Wextra -Wno-unused-parameter -fPIC)
endif()

include_directories(include)
include_directories(src/core)
include_directories(src/platform)
include_directories(src/pacer)

set(TSSHAPER_SOURCES
    src/core/tsshaper.c
    src/core/tstd_model.c
    src/core/statmux.c
    src/core/interleaver.c
    src/core/spsc_queue.c
    src/pacer/pacer_loop.c
    src/platform/hal.c
    src/platform/hal_linux.c
    src/platform/hal_mock.c
    src/platform/hal_callback.c
)

add_library(tsshaper STATIC ${TSSHAPER_SOURCES})
target_link_libraries(tsshaper pthread m)

# Build test_bench as a primary target for validation
add_executable(test_bench tests/test_bench.c)
target_link_libraries(test_bench tsshaper pthread m)

# Automation: make check-jitter
# Performs end-to-end nanosecond-precision PCR jitter validation using Virtual Time Domain
add_custom_target(check-jitter
    COMMAND ./test_bench
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/pcr_analyzer.py virtual_test.pcap --pid 0x100
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running professional PCR jitter compliance audit..."
    DEPENDS test_bench
)
