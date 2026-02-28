# Implementation Plan: Docs Alignment (First 10 Documents)

This plan outlines the sequential implementation of the TsAnalyzer core engine to achieve full alignment with the "Instrument-Grade" specifications defined in Docs 00-09.

## Phase 1: Foundation & Deterministic Runtime (Docs 01, 02, 05)
**Goal**: Establish the "Constitutional" execution environment where all analysis is bit-identical and hardware-timed.

- [ ] **Task 1.1: Fixed-Function Pipeline & CPU Affinity (Doc 01)**
    - [ ] **Implementation**: Refactor `src/tsa_main.c` to implement the 4-thread pipeline (Capture, Decode, Metrology, Output).
    - [ ] **Requirement**: Use `pthread_setaffinity_np` to lock each thread to a specific physical core.
    - [ ] **Verification**: Confirm no thread migration via `top` or `ps` during high-load tests.
- [ ] **Task 1.2: SPSC Queues & Linear Ownership (Doc 01)**
    - [ ] **Implementation**: Implement wait-free SPSC (Single-Producer Single-Consumer) queues for inter-thread communication.
    - [ ] **Requirement**: Enforce the "Linear Ownership Rule" where packet memory is never shared or copied between stages.
- [ ] **Task 1.3: Hardware Timestamping & Monotonic Clock (Doc 02)**
    - [ ] **Implementation**: Integrate `SO_TIMESTAMPING` for NIC hardware arrival times in the Capture stage.
    - [ ] **Requirement**: Standardize on `CLOCK_MONOTONIC_RAW` for all internal temporal delta calculations.
- [ ] **Task 1.4: Determinism Validation Suite (Doc 05)**
    - [ ] **Implementation**: Create a `scripts/verify_determinism.py` tool.
    - [ ] **Requirement**: Compare JSON metrology outputs from multiple runs of the same PCAP; they MUST be MD5-identical.
- [ ] **Task: Conductor - User Manual Verification 'Phase 1: Foundation & Deterministic Runtime' (Protocol in workflow.md)**

## Phase 2: Timing & Buffer Physics (Docs 02, 03, 07)
**Goal**: Implement the reconstructed STC timeline and the normative T-STD/VBV buffer simulation.

- [ ] **Task 2.1: 27MHz STC Reconstruction & Interpolation (Doc 02)**
    - [ ] **Implementation**: Develop the STC recovery logic in `src/tsa.c` using PCR samples and HAT interpolation.
    - [ ] **Requirement**: Use **fixed-point math** for the interpolation slope to ensure cross-platform bit-identity.
- [ ] **Task 2.2: Access Unit (AU) Extraction & PES Parsing (Doc 03)**
    - [ ] **Implementation**: Enhance the Decode stage to reassemble PES packets and extract AUs with DTS/PTS.
    - [ ] **Requirement**: Map each AU to its specific `arrival_vstc` on the reconstructed timeline.
- [ ] **Task 2.3: T-STD Leaky Bucket Simulator (Doc 03)**
    - [ ] **Implementation**: Implement the continuous leaky bucket algorithm for TB, MB, and EB stages.
    - [ ] **Requirement**: Trigger overflow/underflow events strictly based on VSTC timestamps and fixed-point fullness math.
- [ ] **Task 2.4: Forensic Error Model & CC Tracker (Doc 07)**
    - [ ] **Implementation**: Implement the "Forensic Preservation" model for CC gaps and ingestion loss.
    - [ ] **Requirement**: Prohibit silent resynchronization; all transport defects must be exposed in the metrology output.
- [ ] **Task: Conductor - User Manual Verification 'Phase 2: Timing & Buffer Physics' (Protocol in workflow.md)**

## Phase 3: Metrology Engine & Causality (Docs 04, 06)
**Goal**: Implement the TR 101 290 metrology suite and predictive "Remaining Safe Time" diagnostics.

- [ ] **Task 3.1: TR 101 290 P1/P2/P3 Implementation (Doc 04)**
    - [ ] **Implementation**: Develop the priority check engine in the Metrology stage.
    - [ ] **Requirement**: Align all measurement logic with the mathematical definitions in Doc 16 (Metrology Spec).
- [ ] **Task 3.2: RST (Remaining Safe Time) Derivation (Doc 04)**
    - [ ] **Implementation**: Implement the predictive math for RST based on real-time buffer occupancy and drain rates.
    - [ ] **Requirement**: Output RST as a primary diagnostic metric in the JSON API.
- [ ] **Task 3.3: Performance Optimization (1.2M PPS) (Doc 06)**
    - [ ] **Implementation**: Optimize the pipeline for NUMA locality and L2-cache efficiency.
    - [ ] **Requirement**: Benchmark and verify 1.2M PPS throughput on target hardware without dropping packets.
- [ ] **Task 3.4: Causal Analysis Scoring (Doc 04)**
    - [ ] **Implementation**: Implement the Health Score logic that correlates transport defects with buffer stress.
- [ ] **Task: Conductor - User Manual Verification 'Phase 3: Metrology Engine & Causality' (Protocol in workflow.md)**

## Phase 4: Operational Readiness & Validation (Docs 08, 09, 00)
**Goal**: Finalize operational modes and the self-verification framework for production deployment.

- [ ] **Task 4.1: Operational Mode Switching (Doc 09)**
    - [ ] **Implementation**: Implement Live, Replay, and Forensic modes with their respective trust level reporting.
- [ ] **Task 4.2: Self-Verification Framework (Doc 08)**
    - [ ] **Implementation**: Integrate automated accuracy and repeatability tests into the CI/CD pipeline.
    - [ ] **Requirement**: Use hardware-reference logs for comparison during verification.
- [ ] **Task 4.3: Product Identity & Documentation Sync (Doc 00)**
    - [ ] **Implementation**: Final audit of all CLI flags and API outputs to ensure consistency with the Product Overview.
- [ ] **Task: Conductor - User Manual Verification 'Phase 4: Operational Readiness & Validation' (Protocol in workflow.md)**
