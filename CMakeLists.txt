cmake_minimum_required(VERSION 3.10)
project(tspacer C CXX)

enable_testing()

set(CMAKE_C_STANDARD 11)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -march=native -DNDEBUG")
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -g -Wall -Wextra -Werror")
endif()

# SRT Configuration
set(ENABLE_SHARED OFF CACHE BOOL "Disable shared library" FORCE)
set(ENABLE_STATIC ON CACHE BOOL "Enable static library" FORCE)
set(ENABLE_APPS OFF CACHE BOOL "Disable apps" FORCE)
set(ENABLE_TESTING ON CACHE BOOL "Enable testing" FORCE)
set(ENABLE_CodeCoverage OFF CACHE BOOL "Disable code coverage" FORCE)
set(USE_STATIC_LIBSTDCXX ON CACHE BOOL "Use static libstdc++ for air-gapped deployments" FORCE)

add_subdirectory(deps/srt)
include_directories(include deps/mongoose deps/srt/srtcore ${CMAKE_CURRENT_BINARY_DIR}/deps/srt)

add_library(tsp_core STATIC src/tsp.c src/tsa.c src/tsa_clock.c src/tsa_psi.c src/tsa_es.c src/tsa_exporter_prom.c src/srt_utils.c src/tsa_gateway.c src/srt_ingest.c src/dummy.cpp src/alloc_tracker.c src/spsc_queue.c src/mpmc_queue.c)
target_link_libraries(tsp_core srt_static pthread m dl ssl crypto)

add_executable(tsp src/tsp_main.c)
add_executable(tsa_cli src/tsa_cli.c deps/mongoose/mongoose.c)
target_link_libraries(tsa_cli tsp_core srt_static pthread m dl ssl crypto)
# Disable strict warnings for tsa_cli to accommodate mongoose
if(MSVC)
    target_compile_options(tsa_cli PRIVATE /W3)
else()
    target_compile_options(tsa_cli PRIVATE -Wno-all -Wno-extra -Wno-error)
endif()

add_executable(tsg src/tsg_main.c deps/mongoose/mongoose.c)
target_link_libraries(tsg tsp_core srt_static pthread m dl ssl crypto)
if(NOT MSVC)
    target_compile_options(tsg PRIVATE -Wno-all -Wno-extra -Wno-error)
endif()

add_executable(tsa_server src/tsa_server.c src/tsa_exporter_prom.c deps/mongoose/mongoose.c)
target_link_libraries(tsa_server tsp_core srt_static pthread m dl ssl crypto)
if(NOT MSVC)
    target_compile_options(tsa_server PRIVATE -Wno-all -Wno-extra -Wno-error)
endif()

add_executable(tsa_server_pro src/tsa_server_pro.c src/tsa_exporter_prom.c deps/mongoose/mongoose.c)
target_link_libraries(tsa_server_pro tsp_core srt_static pthread m dl ssl crypto)
if(NOT MSVC)
    target_compile_options(tsa_server_pro PRIVATE -Wno-all -Wno-extra -Wno-error)
endif()

add_executable(tsb src/tsb_main.c)
target_link_libraries(tsb tsp_core pthread m dl)
if(NOT MSVC)
    target_compile_options(tsb PRIVATE -Wno-all -Wno-extra -Wno-error)
endif()

add_executable(test_prometheus tests/tsa_test_prometheus.c)
target_link_libraries(test_prometheus tsp_core pthread)
add_test(NAME test_prometheus COMMAND test_prometheus)
target_link_libraries(tsp tsp_core pthread)


add_executable(test_server_hpc tests/test_server_hpc.c)
target_link_libraries(test_server_hpc tsp_core pthread)
add_test(NAME test_server_hpc COMMAND test_server_hpc)

add_executable(test_spsc_queue tests/test_spsc_queue.c)
target_link_libraries(test_spsc_queue tsp_core pthread)
add_test(NAME test_spsc_queue COMMAND test_spsc_queue)

add_executable(test_spsc tests/test_spsc.c)
target_link_libraries(test_spsc tsp_core pthread)
add_test(NAME test_spsc COMMAND test_spsc)

add_executable(test_entropy tests/test_entropy.c)
target_link_libraries(test_entropy tsp_core pthread m)
add_test(NAME test_entropy COMMAND test_entropy)

add_executable(test_pcr_piecewise tests/test_pcr_piecewise.c)
target_link_libraries(test_pcr_piecewise tsp_core ${SRT_LIBRARIES} ${EXTRA_LIBS} m)
add_test(NAME test_pcr_piecewise COMMAND test_pcr_piecewise)

add_executable(test_tr101290 tests/test_tr101290.c)
target_link_libraries(test_tr101290 tsp_core pthread)
add_test(NAME test_tr101290 COMMAND test_tr101290)

add_executable(test_latched_errors tests/test_latched_errors.c)
target_link_libraries(test_latched_errors tsp_core pthread)
add_test(NAME test_latched_errors COMMAND test_latched_errors)

add_executable(test_normalization tests/test_normalization.c)
target_link_libraries(test_normalization tsp_core pthread)
add_test(NAME test_normalization COMMAND test_normalization)

add_executable(test_atomic_sync tests/test_atomic_sync.c)
target_link_libraries(test_atomic_sync tsp_core pthread)
add_test(NAME test_atomic_sync COMMAND test_atomic_sync)

add_executable(test_ema_smoothing tests/test_ema_smoothing.c)
target_link_libraries(test_ema_smoothing tsp_core pthread)
add_test(NAME test_ema_smoothing COMMAND test_ema_smoothing)

add_executable(test_snapshot tests/test_snapshot.c)
target_link_libraries(test_snapshot tsp_core pthread)
add_test(NAME test_snapshot COMMAND test_snapshot)

add_executable(test_integration tests/test_integration.c)

target_link_libraries(test_integration tsp_core pthread)
add_test(NAME test_integration COMMAND test_integration)

add_executable(test_psi_multi tests/test_psi_multi.c)
target_link_libraries(test_psi_multi tsp_core pthread m)
add_test(NAME test_psi_multi COMMAND test_psi_multi)

add_executable(test_psi tests/test_psi.c)
target_link_libraries(test_psi tsp_core pthread m)
add_test(NAME test_psi COMMAND test_psi)

add_executable(test_config tests/test_config.c)
target_link_libraries(test_config tsp_core pthread)
add_test(NAME test_config COMMAND test_config)

add_executable(test_affinity tests/test_affinity.c)
target_link_libraries(test_affinity tsp_core pthread)
add_test(NAME test_affinity COMMAND test_affinity)

add_executable(test_memory tests/test_memory.c)
target_link_libraries(test_memory tsp_core pthread)
add_test(NAME test_memory COMMAND test_memory)

add_executable(test_mem_pool tests/test_mem_pool.c)
target_link_libraries(test_mem_pool tsp_core pthread)
add_test(NAME test_mem_pool COMMAND test_mem_pool)

add_executable(test_pid_tracker tests/test_pid_tracker.c)
target_link_libraries(test_pid_tracker tsp_core pthread)
add_test(NAME test_pid_tracker COMMAND test_pid_tracker)

add_executable(test_pid_protection tests/test_pid_protection.c)
target_link_libraries(test_pid_protection tsp_core pthread)
add_test(NAME test_pid_protection COMMAND test_pid_protection)

add_executable(test_pid_tracker_stress tests/test_pid_tracker_stress.c)
target_link_libraries(test_pid_tracker_stress tsp_core pthread)
add_test(NAME test_pid_tracker_stress COMMAND test_pid_tracker_stress)


add_executable(test_rst_math tests/test_rst_math.c)
target_link_libraries(test_rst_math tsp_core pthread m)
add_test(NAME test_rst_math COMMAND test_rst_math)

add_executable(test_rca_scoring tests/test_rca_scoring.c)
target_link_libraries(test_rca_scoring tsp_core pthread m)
add_test(NAME test_rca_scoring COMMAND test_rca_scoring)

add_executable(test_rca_scoring_v2 tests/test_rca_scoring_v2.c)
target_link_libraries(test_rca_scoring_v2 tsp_core pthread m)
add_test(NAME test_rca_scoring_v2 COMMAND test_rca_scoring_v2)

add_executable(test_snapshot_consistency tests/test_snapshot_consistency.c)
target_link_libraries(test_snapshot_consistency tsp_core pthread)
add_test(NAME test_snapshot_consistency COMMAND test_snapshot_consistency)

add_executable(test_stc_reconstruction tests/test_stc_reconstruction.c)
target_link_libraries(test_stc_reconstruction tsp_core pthread m)
add_test(NAME test_stc_reconstruction COMMAND test_stc_reconstruction)

add_executable(test_pcr_jitter tests/test_pcr_jitter.c)
target_link_libraries(test_pcr_jitter tsp_core pthread m)
add_test(NAME test_pcr_jitter COMMAND test_pcr_jitter)

add_executable(test_latency_p99 tests/test_latency_p99.c)
target_link_libraries(test_latency_p99 tsp_core pthread)
add_test(NAME test_latency_p99 COMMAND test_latency_p99)

add_executable(test_jitter_burn tests/test_jitter_burn.c)
add_test(NAME test_jitter_burn COMMAND test_jitter_burn)

add_executable(test_udp_stability tests/test_udp_stability.c)
target_link_libraries(test_udp_stability tsp_core pthread m)

add_executable(test_tspacer_callback tests/test_tspacer_callback.c)
target_link_libraries(test_tspacer_callback tsp_core pthread m)

add_executable(test_warmup tests/test_warmup.c)
target_link_libraries(test_warmup tsp_core pthread)
add_test(NAME test_warmup COMMAND test_warmup)

add_executable(test_governor tests/test_governor.c)
target_link_libraries(test_governor tsp_core pthread)
add_test(NAME test_governor COMMAND test_governor)

add_executable(test_pcr tests/tsa_test_pcr.c)
target_link_libraries(test_pcr tsp_core srt_static pthread dl ssl crypto)
add_test(NAME test_pcr COMMAND test_pcr)

add_executable(test_estimator tests/test_estimator.c)
target_link_libraries(test_estimator tsp_core pthread m)
target_compile_definitions(test_estimator PRIVATE RING_BUFFER_SIZE=1024)
add_test(NAME test_estimator COMMAND test_estimator)

add_executable(test_pacing tests/tsp_test_pacing.c)
target_link_libraries(test_pacing tsp_core pthread m)
target_compile_definitions(test_pacing PRIVATE RING_BUFFER_SIZE=1024)
add_test(NAME test_pacing COMMAND test_pacing)

add_executable(test_telemetry tests/test_telemetry.c)
target_link_libraries(test_telemetry tsp_core pthread m)
target_compile_definitions(test_telemetry PRIVATE RING_BUFFER_SIZE=1024)
add_test(NAME test_telemetry COMMAND test_telemetry)

add_executable(test_pcr_mapping tests/test_pcr_mapping.c)
target_link_libraries(test_pcr_mapping tsp_core srt_static pthread m dl ssl crypto)
add_test(NAME test_pcr_mapping COMMAND test_pcr_mapping)

add_executable(test_analyzer_zero_alloc tests/test_analyzer_zero_alloc.c)
target_link_libraries(test_analyzer_zero_alloc tsp_core srt_static pthread m dl ssl crypto)
add_test(NAME test_analyzer_zero_alloc COMMAND test_analyzer_zero_alloc)

add_executable(test_mpmc_ready_queue tests/test_mpmc_ready_queue.c)
target_link_libraries(test_mpmc_ready_queue tsp_core srt_static pthread m dl ssl crypto)
add_test(NAME test_mpmc_ready_queue COMMAND test_mpmc_ready_queue)

add_executable(test_timer_precision tests/test_timer_precision.c)
target_link_libraries(test_timer_precision pthread m)
add_test(NAME test_timer_precision COMMAND test_timer_precision)

add_executable(test_hybrid_sleep tests/test_hybrid_sleep.c)
target_link_libraries(test_hybrid_sleep pthread m)
add_test(NAME test_hybrid_sleep COMMAND test_hybrid_sleep)

add_executable(test_catchup tests/test_catchup.c)
target_link_libraries(test_catchup srt_static pthread m dl ssl crypto)
add_test(NAME test_catchup COMMAND test_catchup)

add_executable(test_metrology_utils tests/test_metrology_utils.c)
target_link_libraries(test_metrology_utils tsp_core pthread m)
add_test(NAME test_metrology_utils COMMAND test_metrology_utils)

add_executable(test_pcr_engine tests/test_pcr_engine.c)
target_link_libraries(test_pcr_engine tsp_core pthread m)
add_test(NAME test_pcr_engine COMMAND test_pcr_engine)

add_executable(test_cc_classifier tests/test_cc_classifier.c)
target_link_libraries(test_cc_classifier tsp_core pthread m)
add_test(NAME test_cc_classifier COMMAND test_cc_classifier)

add_executable(test_tstd_model tests/test_tstd_model.c)
target_link_libraries(test_tstd_model tsp_core pthread m)
add_test(NAME test_tstd_model COMMAND test_tstd_model)

add_executable(test_pmt_reset tests/test_pmt_reset.c)
target_link_libraries(test_pmt_reset tsp_core pthread m)
add_test(NAME test_pmt_reset COMMAND test_pmt_reset)

add_executable(test_fill_pct tests/test_fill_pct.c)
target_link_libraries(test_fill_pct tsp_core pthread m)
add_test(NAME test_fill_pct COMMAND test_fill_pct)

add_executable(test_rst_tstd tests/test_rst_tstd.c)
target_link_libraries(test_rst_tstd tsp_core pthread m)
add_test(NAME test_rst_tstd COMMAND test_rst_tstd)

add_executable(test_es_metadata tests/test_es_metadata.c)
target_link_libraries(test_es_metadata tsp_core pthread m)
add_test(NAME test_es_metadata COMMAND test_es_metadata)

add_executable(test_fast_itoa tests/test_fast_itoa.c)
target_link_libraries(test_fast_itoa tsp_core pthread m)
add_test(NAME test_fast_itoa COMMAND test_fast_itoa)

add_executable(test_metric_buffer tests/test_metric_buffer.c)
target_link_libraries(test_metric_buffer tsp_core pthread m)
add_test(NAME test_metric_buffer COMMAND test_metric_buffer)

add_executable(test_metric_bench tests/test_metric_bench.c)
target_link_libraries(test_metric_bench tsp_core pthread m)
add_test(NAME test_metric_bench COMMAND test_metric_bench)

add_executable(test_prom_export tests/test_prom_registry.c)
target_link_libraries(test_prom_export tsp_core pthread m)
add_test(NAME test_prom_export COMMAND test_prom_export)
add_executable(test_prom_labels tests/test_prom_labels.c)
target_link_libraries(test_prom_labels tsp_core pthread m)
add_test(NAME test_prom_labels COMMAND test_prom_labels)

add_executable(test_tstd_reset tests/test_tstd_reset.c)
target_link_libraries(test_tstd_reset tsp_core pthread m)
add_test(NAME test_tstd_reset COMMAND test_tstd_reset)

add_executable(test_tstd_underflow tests/test_tstd_underflow.c)
target_link_libraries(test_tstd_underflow tsp_core pthread m)
add_test(NAME test_tstd_underflow COMMAND test_tstd_underflow)

add_executable(test_tstd_expert tests/test_tstd_expert.c)
target_link_libraries(test_tstd_expert tsp_core pthread m)
add_test(NAME test_tstd_expert COMMAND test_tstd_expert)

add_executable(test_stc_stability tests/test_stc_stability.c)
target_link_libraries(test_stc_stability tsp_core pthread m)
add_test(NAME test_stc_stability COMMAND test_stc_stability)

add_executable(test_packet_ring tests/test_packet_ring.c)
target_link_libraries(test_packet_ring tsp_core pthread m)
add_test(NAME test_packet_ring COMMAND test_packet_ring)

add_executable(test_forensic_writer tests/test_forensic_writer.c)
target_link_libraries(test_forensic_writer tsp_core pthread m)
add_test(NAME test_forensic_writer COMMAND test_forensic_writer)

add_executable(test_capture_performance tests/test_capture_performance.c)
target_link_libraries(test_capture_performance tsp_core pthread m)
add_test(NAME test_capture_performance COMMAND test_capture_performance)

add_executable(test_forensic_json tests/test_forensic_json.c)
target_link_libraries(test_forensic_json tsp_core pthread m)
add_test(NAME test_forensic_json COMMAND test_forensic_json)

add_executable(test_rst_network tests/test_rst_network.c)
target_link_libraries(test_rst_network tsp_core pthread m)
add_test(NAME test_rst_network COMMAND test_rst_network)

add_executable(test_rst_encoder tests/test_rst_encoder.c)
target_link_libraries(test_rst_encoder tsp_core pthread m)
add_test(NAME test_rst_encoder COMMAND test_rst_encoder)

add_executable(test_mdi_engine tests/test_mdi_engine.c)
target_link_libraries(test_mdi_engine tsp_core pthread m)
add_test(NAME test_mdi_engine COMMAND test_mdi_engine)

add_executable(test_tr101290_p2 tests/test_tr101290_p2.c)
target_link_libraries(test_tr101290_p2 tsp_core pthread m)
add_test(NAME test_tr101290_p2 COMMAND test_tr101290_p2)

add_executable(test_tr101290_p1 tests/test_tr101290_p1.c)
target_link_libraries(test_tr101290_p1 tsp_core pthread m)
add_test(NAME test_tr101290_p1 COMMAND test_tr101290_p1)

add_executable(test_gateway_fail_safe tests/test_gateway_fail_safe.c)
target_link_libraries(test_gateway_fail_safe tsp_core pthread m)
add_test(NAME test_gateway_fail_safe COMMAND test_gateway_fail_safe)

add_executable(test_gateway_performance tests/test_gateway_performance.c)
target_link_libraries(test_gateway_performance tsp_core pthread m)
add_test(NAME test_gateway_performance COMMAND test_gateway_performance)

add_executable(test_gateway_handoff tests/test_gateway_handoff.c)
target_link_libraries(test_gateway_handoff tsp_core pthread m)
add_test(NAME test_gateway_handoff COMMAND test_gateway_handoff)

add_executable(test_dejitter_performance tests/test_dejitter_performance.c)
target_link_libraries(test_dejitter_performance tsp_core pthread m)
add_test(NAME test_dejitter_performance COMMAND test_dejitter_performance)

add_executable(test_cc_repair tests/test_cc_repair.c)
target_link_libraries(test_cc_repair tsp_core pthread m)
add_test(NAME test_cc_repair COMMAND test_cc_repair)

add_executable(test_cc_repair_stress tests/test_cc_repair_stress.c)
target_link_libraries(test_cc_repair_stress tsp_core pthread m)
add_test(NAME test_cc_repair_stress COMMAND test_cc_repair_stress)

add_executable(test_srt_aes_performance tests/test_srt_aes_performance.c)
target_link_libraries(test_srt_aes_performance tsp_core srt_static pthread m dl ssl crypto)
add_test(NAME test_srt_aes_performance COMMAND test_srt_aes_performance)

add_executable(test_clock_128 tests/test_clock_128.c)
target_link_libraries(test_clock_128 tsp_core pthread)
add_test(NAME test_clock_128 COMMAND test_clock_128)

add_executable(test_seqlock_fuzz tests/test_seqlock_fuzz.c)
target_link_libraries(test_seqlock_fuzz tsp_core pthread)
add_test(NAME test_seqlock_fuzz COMMAND test_seqlock_fuzz)

add_executable(test_server_robustness tests/test_server_robustness.c)
target_link_libraries(test_server_robustness tsp_core pthread m)
add_test(NAME test_server_robustness COMMAND test_server_robustness)

add_executable(test_gateway_create tests/test_gateway_create.c)
target_link_libraries(test_gateway_create tsp_core pthread m)
add_test(NAME test_gateway_create COMMAND test_gateway_create)

add_executable(test_causal_chain tests/test_causal_chain.c)
target_link_libraries(test_causal_chain tsp_core pthread m)
add_test(NAME test_causal_chain COMMAND test_causal_chain)

add_executable(test_json_serialization tests/test_json_serialization.c)
target_link_libraries(test_json_serialization tsp_core pthread m)
add_test(NAME test_json_serialization COMMAND test_json_serialization)

add_executable(test_health_score tests/test_health_score.c)
target_link_libraries(test_health_score tsp_core pthread m)
add_test(NAME test_health_score COMMAND test_health_score)

add_executable(test_rst_prediction tests/test_rst_prediction.c)
target_link_libraries(test_rst_prediction tsp_core pthread m)
add_test(NAME test_rst_prediction COMMAND test_rst_prediction)

add_executable(test_bitrate_comparison tests/test_bitrate_comparison.c)
target_link_libraries(test_bitrate_comparison tsp_core pthread m)
add_test(NAME test_bitrate_comparison COMMAND test_bitrate_comparison)

add_executable(test_debounce tests/test_debounce.c)
target_link_libraries(test_debounce tsp_core pthread m)
add_test(NAME test_debounce COMMAND test_debounce)

add_executable(test_es_deep_analysis tests/test_es_deep_analysis.c)
target_link_libraries(test_es_deep_analysis tsp_core pthread m)
add_test(NAME test_es_deep_analysis COMMAND test_es_deep_analysis)

add_executable(test_psi_assembly tests/test_psi_assembly.c)
target_link_libraries(test_psi_assembly tsp_core pthread m dl ssl crypto)
add_test(NAME test_psi_assembly COMMAND test_psi_assembly)

add_executable(test_fragmented_psi tests/test_fragmented_psi.c)
target_link_libraries(test_fragmented_psi tsp_core pthread m dl ssl crypto)
add_test(NAME test_fragmented_psi COMMAND test_fragmented_psi)

add_executable(test_crc_error_psi tests/test_crc_error_psi.c)
target_link_libraries(test_crc_error_psi tsp_core pthread m dl ssl crypto)
add_test(NAME test_crc_error_psi COMMAND test_crc_error_psi)

add_executable(test_psi_version_rollover tests/test_psi_version_rollover.c)
target_link_libraries(test_psi_version_rollover tsp_core pthread m dl ssl crypto)
add_test(NAME test_psi_version_rollover COMMAND test_psi_version_rollover)

add_executable(test_clock_inspector tests/test_clock_inspector.c)
target_link_libraries(test_clock_inspector tsp_core pthread m)
add_test(NAME test_clock_inspector COMMAND test_clock_inspector)

# --- Professional Tier Tests ---
add_executable(test_crc_pro tests/test_crc_pro.c)
target_link_libraries(test_crc_pro tsp_core pthread)
add_test(NAME test_crc_pro COMMAND test_crc_pro)

add_executable(test_tstd_btvhd tests/test_tstd_btvhd.c)
target_link_libraries(test_tstd_btvhd tsp_core)
add_test(NAME test_tstd_btvhd COMMAND test_tstd_btvhd)

add_executable(test_tstd_hevc tests/test_tstd_hevc.c)
target_link_libraries(test_tstd_hevc tsp_core m pthread)
add_test(NAME test_tstd_hevc COMMAND test_tstd_hevc)

add_executable(test_gateway_pro tests/test_gateway_pro.c)
target_link_libraries(test_gateway_pro tsp_core m pthread)
add_test(NAME test_gateway_pro COMMAND test_gateway_pro)

add_executable(test_gateway_forensics tests/test_gateway_forensics.c)
target_link_libraries(test_gateway_forensics tsp_core m pthread)
add_test(NAME test_gateway_forensics COMMAND test_gateway_forensics)

add_executable(test_server_correlation tests/test_server_correlation.c)
target_link_libraries(test_server_correlation tsp_core m pthread)
add_test(NAME test_server_correlation COMMAND test_server_correlation)

add_executable(test_qoe_pro tests/test_qoe_pro.c)
target_link_libraries(test_qoe_pro tsp_core m pthread)
add_test(NAME test_qoe_pro COMMAND test_qoe_pro)
add_executable(test_mem_guard tests/test_mem_guard.c)
target_link_libraries(test_mem_guard tsp_core m pthread)