# TsAnalyzer Scripts Directory

This directory contains production-grade validation, deployment, and stress-testing tools for the TsAnalyzer ecosystem.

## 1. Metrology & Audit (TR 101 290)
*   `pcr_analyzer.py`: Professional PCAP/TS PCR jitter analyzer (Nanosecond precision).
*   `pcr_analyzer_lite.py`: Lightweight TS-only PCR sampler (Zero-dependency).
*   `verify_shaper_compliance.sh`: Validates libtsshaper output against T-STD leak rates.
*   `verify_pcr_repetition.sh`: Checks PCR arrival intervals against 40ms limit.

## 2. End-to-End Validation (E2E)
*   `test_tsshaper_e2e.sh`: Full FFmpeg + libtsshaper integration test.
*   `test-e2e-srt.sh`: Validates SRT ingest and shaping.
*   `verify_network_io.sh`: Tests HAL-layer socket performance and packet loss.
*   `test_server_pro.sh`: Validates the high-performance multi-stream conductor.

## 3. Stability & Stress
*   `professional_stress_runner.py`: Orchestrates multi-stream load testing.
*   `extreme_stress_test.py`: Boundary testing for buffer overflows and resource exhaustion.
*   `verify_5m_soak.sh`: 5-minute stability audit for regression testing.
*   `test_stability_5min_simple.py`: Basic concurrency and memory leak check.

## 4. System & Environment
*   `sys_tune_perf.sh`: Tunes NIC, CPU, and IRQ for low-latency processing.
*   `grant_rt_perms.sh`: Enables REALTIME scheduling for non-root users.
*   `env_ready.sh`: Verifies that the OS environment meets TSA prerequisites.

## 5. Observability
*   `deploy_dashboard.py`: Automatically provisions Grafana dashboards.
*   `metrics_exporter.py`: Bridges internal TSA metrics to Prometheus.
*   `tsa_ascii_monitor.sh`: Terminal-based real-time telemetry viewer.

## 6. Chaos & Debug
*   `chaos_proxy.py`: Simulates network impairments (jitter, drop, reorder).
*   `simulate_mdi_srt_incident.py`: Reconstructs specific field-reported issues.
