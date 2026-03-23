# Implementation Plan: libtsshaper (Professional TS Shaper)

## 1. Overview
This document outlines the step-by-step implementation strategy for `libtsshaper`. Following a strict **Test-Driven Development (TDD)** and **Layered Validation** approach, we prioritize building the verification harness before implementing the core shaping logic.

## 2. Phase 1: High-Precision Verification Harness (Current Priority)
**Goal**: Establish the "Referee" before the "Player". Ensure 100% deterministic compliance checking.

- **Task 1.1: Nanosecond PCR Analyzer (`scripts/pcr_analyzer.py`)**
  - Implement a Python script using `scapy` to parse PCAPNG files.
  - Calculate PCR Jitter relative to physical capture time ($T_{pcap}$).
  - **Threshold**: Fail if Max Jitter > 30ns or PCR Interval > 40ms.
- **Task 1.2: Containerized DVB Compliance Suite**
  - Configure `TSDuck` within a Docker environment (`Dockerfile.tsduck`).
  - Automate TR 101 290 Priority 1/2/3 semantic scanning.
- **Task 1.3: HAL Abstraction Layer**
  - Define `src/platform/hal.h` for clock and I/O.
  - Create a `Mock HAL` for Virtual Time Domain testing.

## 3. Phase 2: Core Logic & Standalone Validation
**Goal**: Verify mathematical models (PI Controller) and data structures (SPSC Queue) in isolation.

- **Task 2.1: Q16.16 Fixed-Point PI Controller**
  - Implement the PI logic in `src/core/tstd_model.c`.
  - Validate Anti-Windup and Deadband stability using a standalone test driver.
- **Task 2.2: Lock-Free SIMD Buffer**
  - Implement the SPSC queue with `alignas(128)` and AVX2 copy.
  - Conduct multi-threaded stress tests to verify memory barrier integrity.
- **Task 2.3: PCAPNG Mock Exporter**
  - Enable the engine to "emit" packets directly to a PCAPNG file with nanosecond timestamps ($T_{emit}$).
  - Verify compliance using the Phase 1 Analyzer.

## 4. Phase 3: Real-Time Pacer & I/O Engine
**Goal**: Achieve low-jitter physical emission on Linux.

- **Task 3.1: Three-Stage Precision Pacer**
  - Implement the `poll()` -> `sched_yield()` -> `cpu_relax()` (PAUSE) loop.
  - Bind the thread to a dedicated CPU core (Affinity).
- **Task 3.2: JIT Batching with `sendmmsg`**
  - Implement SMPTE 2022-2 (7 TS per UDP) aggregation.
  - Integrate with Linux FQ qdisc via `SO_MAX_PACING_RATE`.

## 5. Phase 4: FFmpeg Ecosystem Integration
**Goal**: Full end-to-end professional encoding pipeline.

- **Task 4.1: AVIOContext Adapter**
  - Develop a custom FFmpeg I/O protocol handler (`tsshaper://`).
  - Route raw VBR TS chunks from FFmpeg Muxer into `libtsshaper`.
- **Task 4.2: Semantic Hinting Bypass**
  - Peek at PIDs in the AVIO callback to pass `TSS_PID_TYPE` hints to the engine.
- **Task 4.3: End-to-End Field Test**
  - Encode a 4K live stream via FFmpeg + libtsshaper.
  - Perform 24h stability audit using professional hardware analyzers.

## 6. Success Criteria (Definition of Done)
- **Compliance**: Zero TR 101 290 errors reported by TSDuck.
- **Precision**: Max PCR Jitter < 30ns verified by Python Analyzer.
- **Performance**: Single-core throughput > 1 Gbps on standard x86_64.
- **Stability**: Zero packet loss or buffer overflow during a 24-hour "I-Frame Bomb" stress test.
