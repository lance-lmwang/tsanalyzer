# Implementation Plan: Industrial Integration (ltntstools)

## Phase 1: P0 - Clock Drift & Pacer Refinement (Stability)
- [x] **Step 1**: Implement Clock Drift Compensation in `src/tsp.c`. Adjust sending rate based on system clock vs. PCR drift.
- [x] **Step 2**: Refine PCR-Locked scheduling logic to handle stream discontinuities more gracefully.

## Phase 2: P1 - Stream Modeling & Descriptor Factory (Extensibility)
- [x] **Step 3**: Refactor `src/tsa_psi.c` to use a tree-based stream model similar to `streammodel.c`.
- [x] **Step 4**: Implement the Descriptor Factory. Migrate AC3 and Subtitle detection to this new framework.
- [x] **Step 5**: Add support for LCN (Logical Channel Number) and service name extraction via descriptors.

## Phase 3: P1 - Compliance & Health (Quality)
- [x] **Step 6**: Upgrade TR 101 290 state machine with professional timer-based debounce logic.
- [x] **Step 7**: Integrate the health scoring model from `tr101290-summary.c`.

## Phase 4: P2 - High-Resolution Metrology (Observability)
- [x] **Step 8**: Implement sliding-window bitrate histograms.
- [x] **Step 9**: Export `Peak Bitrate (Max Mbps)` and `Jitter Distribution` to Prometheus.

## Phase 5: Zero-Alloc Memory Optimization (Performance)
- [x] **Step 10**: Introduce Side-band Metadata in `tsa_packet_pool`.
- [x] **Step 11**: Replace high-frequency `malloc` in engine instance creation with a pre-allocated pool.
