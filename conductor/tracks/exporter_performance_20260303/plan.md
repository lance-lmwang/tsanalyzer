# Implementation Plan: Exporter Performance Optimization (4-Tier)

This track focuses on drastically improving the performance of the metrics export path to support high-density industrial deployments without cache-thrashing or lock contention.

## Phase 1: Zero Allocation Path (Eliminate `malloc`)
- [x] **Step 1.1**: Modify `tsa_exporter_prom_v2` to accept an external scratch buffer for temporary snapshot storage, or use a static/thread-local buffer to eliminate the `malloc(sizeof(tsa_snapshot_full_t))` call.
- [x] **Step 1.2**: Verify via tests (e.g., `test_analyzer_zero_alloc` or `test_server_hpc`) that `malloc` is no longer called during the metric scraping path.
- [x] **Step 1.3**: Commit: `refactor(exporter): eliminate malloc in prometheus export path`

## Phase 2: Zero-Copy Double Buffering (Ping-Pong Model)
- [x] **Step 2.1**: Update `tsa_internal.h` to define a double buffer array `tsa_snapshot_full_t snap_buffers[2]` and an `_Atomic uint8_t active_idx`.
- [x] **Step 2.2**: Modify `tsa_commit_snapshot` to write to `snap_buffers[!active_idx]` and then atomically toggle `active_idx`.
- [x] **Step 2.3**: Update `tsa_take_snapshot_full` to read from the currently active buffer, removing the older `SeqLock` loop (`snap_state.seq`).
- [x] **Step 2.4**: Verify snapshot consistency with existing tests (`test_snapshot_consistency.c`).
- [x] **Step 2.5**: Commit: `perf(core): implement ping-pong double buffering for snapshots`

## Phase 3: Tiered Scraping (Endpoint Split)
- [x] **Step 3.1**: Create `tsa_exporter_prom_core` (only Tier 1-4 summaries) and `tsa_exporter_prom_pids` (only PID details & T-STD).
- [x] **Step 3.2**: Update the Mongoose HTTP server in `src/tsa_server_pro.c` and `src/tsa_main.c` to expose `/metrics/core` and `/metrics/pids`.
- [x] **Step 3.3**: Ensure backward compatibility on `/metrics` (or decide to deprecate it).
- [x] **Step 3.4**: Commit: `feat(api): split prometheus endpoints for tiered scraping`

## Phase 4: Pre-compiled Labels & String Optimizations (Optional/Future)
- [x] **Step 4.1**: Cache PID labels strings inside the `tsa_pid_info_t` struct upon PID discovery.
- [x] **Step 4.2**: Replace `snprintf` with `tsa_fast_itoa` / memory-copies in the hot loops.
- [x] **Step 4.3**: Commit: `perf(exporter): use pre-compiled labels and fast string formatting`
