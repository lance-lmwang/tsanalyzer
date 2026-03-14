# Implementation Plan: Professional Monitoring Matrix

## Phase 1: High-Resolution Distributions (Histograms)
*   [ ] **Task 1.1**: Update `tsa_tr101290_stats_t` to include atomic bins for PCR Jitter.
*   [ ] **Task 1.2**: Refactor `tsa_pcr_track.c` to populate these jitter buckets on every LRM calculation.
*   [ ] **Task 1.3**: Update `tsa_exporter_prom.c` to export standard Prometheus Histogram formats.

## Phase 2: Per-Stream Resource Accounting
*   [ ] **Task 2.1**: Implement `tsa_runtime_stats_t` to track CPU cycles using `rdtsc` or `clock_gettime(THREAD_CPUTIME)`.
*   [ ] **Task 2.2**: Integrate cycle counting into the `tsa_feed_data` entry and exit points.
*   [ ] **Task 2.3**: Report `tsa_system_per_stream_cpu_seconds_total` in Prometheus.

## Phase 3: SRT & Network Deep-Dive
*   [ ] **Task 3.1**: Bridge `srt_bstats` directly to the `tsa_source_t` statistics structure.
*   [ ] **Task 3.2**: Export RTT, Congestion Window, and Retransmit counts.

## Phase 4: Industrial Ecosystem (Dashboards & Alerts)
*   [ ] **Task 4.1**: Create `monitoring/prometheus/tsa_industrial.rules.yml`.
*   [ ] **Task 4.2**: Scaffolding for a Grafana dashboard JSON focusing on PCR Metrology.

## Phase 5: Verification
*   [ ] **Task 5.1**: Stress test with 50+ concurrent streams to verify monitoring overhead is < 2% CPU.
*   [ ] **Task 5.2**: `make full-test` validation.
