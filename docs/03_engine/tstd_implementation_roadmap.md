# Native T-STD Integration: Implementation Roadmap
*Detailed Engineering Phases for Broadcast-Grade Muxer Core*

## Phase 1: Infrastructure & Memory Safety (The Skeleton)
**Goal:** Establish the hierarchical data structures and robust lifecycle management.

- **Task 1.1: Comprehensive Header Update**
  - Update `libavformat/tstd.h` with the finalized `TSTDContext`, `TSTDProgram`, and `TSTDPidState`.
  - Include dual-clock members (`v_stc`, `d_stc`) and AU removal queues (`au_events`).
- **Task 1.2: Memory Scaffold (`ff_tstd_init`)**
  - Implement dynamic allocation for MPTS structures.
  - Initialize the Global Token Pool based on `mux_rate` (100ms headroom).
  - Configure per-PID Token buckets and `BSn` capacities.
- **Task 1.3: Safe Teardown (`ff_tstd_free`)**
  - Implement recursive deep-cleaning logic.
  - Ensure all `AVFifoBuffer` instances are explicitly freed to prevent leaks.
- **Checkpoint:** Build `tstd.o` successfully with zero warnings and verify memory layout.

## Phase 2: Clock Driving & Arrival Dynamics (The Circulation)
**Goal:** Synchronize physical and essence time domains; manage input jitter.

- **Task 2.1: Quantized Clock Advancement**
  - Implement `v_stc += PACKET_TIME_27M` logic.
  - Link `d_stc` to `v_stc` via `mux_delay` offset.
- **Task 2.2: Soft-Landing Arrival Gate**
  - Implement `ff_tstd_enqueue` with `MAX_ARRIVAL_LAG` monitoring.
  - Add emergency reset for DTS backward jumps to prevent pipeline stalls.
- **Task 2.3: Access Unit Hijacking**
  - Add `on_au_muxed` hook to intercept full PES payloads.
  - Populate the `au_events` queue for downstream T-STD simulation.
- **Checkpoint:** Verify `v_stc` linearity and FIFO pressure under randomized DTS inputs.

## Phase 3: Core Control Algorithms (The Brain)
**Goal:** Implement the mathematical feedback loops for PCR and Bitrate stability.

- **Task 3.1: Lyapunov-Stable Scoring**
  - Implement `tstd_program_score` with the `d_debt` damping term.
  - Ensure all units are normalized to `27MHz ticks`.
- **Task 3.2: Bitrate PLL (The Actuator)**
  - Implement the `null_budget` integral controller.
  - Apply anti-windup clamping to the budget integrator.
- **Task 3.3: PCR PIF Controller**
  - Implement Frequency-Locked Loop (FLL) logic.
  - Integrate noise/drift spectrum simulation for realistic jitter signatures.
- **Checkpoint:** Unit test the PLL convergence and verify no-sawtooth bitrate profile.

## Phase 4: Hierarchical Scheduler & Decoder Model (The Soul)
**Goal:** Finalize the decision tree and achieve 100% T-STD conformance.

- **Task 4.1: Level-0 to Level-3 Decision Tree**
  - Implement `tstd_pick_ready_pid` with mandatory PSI/SI (L1) priority.
  - Ensure L1 bypasses token constraints for repetition interval safety.
- **Task 4.2: Discrete Decoder Removal**
  - Implement `tstd_update_tbn` driven by `d_stc`.
  - Add real-time overflow/underflow violation counters.
- **Task 4.3: Integrated Driver Loop (`ff_tstd_step`)**
  - Fuse the double-loop control into a single step engine.
- **Checkpoint:** Validate TBn occupancy traces against industrial analyzers.

## Phase 5: Integration & Full-Chain Validation (The Body)
**Goal:** Seamless FFmpeg integration and passing all industrial gates.

- **Task 5.1: Final mpegtsenc.c Interception**
  - Wire `mpegts_write_packet_internal` to the T-STD engine.
  - Implement the compliance-safe `ff_tstd_drain` for tail closure.
- **Task 5.2: Professional Telemetry Export**
  - Align logging with the `[T-STD]` standard format string.
- **Task 5.3: Acceptance Testing**
  - Run the full validation matrix: `cbr_null`, `burst_iframe`, `long_run`.
  - **KPI Gate:** Max Bitrate Fluctuation < 64kbps; PCR Jitter < 500ns.
- **Checkpoint:** Pass `tstd_telemetry_analyzer.py --ci`.
