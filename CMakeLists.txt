cmake_minimum_required(VERSION 3.10)
project(TsAnalyzer C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -g -Wall -Wextra -pthread")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -g -Wall -Wextra -pthread")

# Resolve mandatory system dependencies (Modern Ubuntu/Debian style)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(CURL REQUIRED)

# Standard shared libraries from the OS (compatible with PIE)
find_library(PCAP_LIB pcap REQUIRED)
find_library(SRT_LIB NAMES srt srt-openssl REQUIRED)
find_library(LUA_LIB NAMES lua5.4 lua5.3 lua REQUIRED)
find_library(NCURSES_LIB ncurses REQUIRED)
find_library(RT_LIB rt REQUIRED)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/src")
include_directories(${OPENSSL_INCLUDE_DIR})
include_directories(${ZLIB_INCLUDE_DIRS})
include_directories(${CURL_INCLUDE_DIRS})

# Include path for SRT and Lua
include_directories("/usr/include/srt")
include_directories("/usr/include/lua5.4")

add_definitions(-DHAVE_PCAP -DHAVE_SRT -DHAVE_CURL -DHAVE_LUA)

set(TSA_EXTERNAL_LIBS
    ${SRT_LIB}
    ${CURL_LIBRARIES}
    ${LUA_LIB}
    ${PCAP_LIB}
    ${OPENSSL_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${RT_LIB}
    ${NCURSES_LIB}
    pthread m dl stdc++
)

set(TSA_CORE_SRCS
    src/tsp.c src/tsa.c src/tsa_engine_tr101290.c
    src/tsa_engine_pcr.c src/tsa_engine_essence.c src/tsa_utils.c src/tsa_json.c
    src/tsa_dashboard.c src/tsa_snapshot.c src/tsa_regress.c src/tsa_clock.c
    src/tsa_psi.c src/tsa_scte35.c src/tsa_engine_scte35.c src/tsa_es.c
    src/tsa_descriptors.c src/tsa_health.c src/tsa_histogram.c
    src/tsa_exporter_prom.c  src/tsa_gateway.c src/srt_ingest.c
    src/tsa_source.c  src/alloc_tracker.c src/spsc_queue.c
    src/mpmc_queue.c src/tsa_stream.c src/tsa_plugin.c src/tsa_event.c
    src/mongoose.c src/nalu_sniffer.c src/tsa_pcr_track.c src/tsa_lua.c
    src/tsa_hls_parser.c src/tsa_hls_ingest.c src/tsa_webhook.c src/tsa_alert.c
    src/tsa_log.c src/tsa_units.c src/tsa_conf_lexer.c src/tsa_conf.c
    src/tsa_auth.c
    src/tsa_pcr_clock.c src/tsa_stream_model.c src/tsa_pipeline.c
    src/tsa_packet_pool.c src/tsa_clock_domain.c src/tsa_simd.c src/tsa_simd_avx2.c
    src/tsa_simd_sse42.c)

add_library(tsp_core STATIC ${TSA_CORE_SRCS})
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(src/tsa_simd_avx2.c PROPERTIES COMPILE_FLAGS "-mavx2")
    set_source_files_properties(src/tsa_simd_sse42.c PROPERTIES COMPILE_FLAGS "-msse4.2")
endif()
target_link_libraries(tsp_core ${TSA_EXTERNAL_LIBS})

macro(tsa_exe name src)
    add_executable(${name} ${src})
    target_link_libraries(${name} tsp_core ${TSA_EXTERNAL_LIBS})
endmacro()

tsa_exe(tsp src/tsp_main.c)
tsa_exe(tsa_cli src/tsa_cli.c)
target_compile_options(tsa_cli PRIVATE -Wno-all -Wno-extra -Wno-error)
tsa_exe(tsg src/tsg_main.c)
tsa_exe(tsa_server src/tsa_server_pro.c)
tsa_exe(tsa_server_pro src/tsa_server_pro.c)
tsa_exe(tsa_top tools/tsa_top.c)
target_link_libraries(tsa_top ${NCURSES_LIB} ${RT_LIB})

enable_testing()
macro(tsa_test name src)
    add_executable(${name} tests/${src})
    target_link_libraries(${name} tsp_core ${TSA_EXTERNAL_LIBS})
    add_test(NAME ${name} COMMAND ${name})
endmacro()

tsa_test(test_analyzer_zero_alloc test_analyzer_zero_alloc.c)
tsa_test(test_psi test_psi.c)
tsa_test(test_alert_state_machine test_alert_state_machine.c)
tsa_test(test_simd test_simd.c)
