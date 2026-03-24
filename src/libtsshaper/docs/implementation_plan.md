# Implementation Plan: libtsshaper (Professional TS Shaper)

## 1. Overview
This document outlines the step-by-step implementation strategy for `libtsshaper`. Following a strict **Test-Driven Development (TDD)** and **Layered Validation** approach, we prioritize building the verification harness before implementing the core shaping logic.

## 2. Phase 1: High-Precision Verification Harness [DONE]
**Goal**: Establish the "Referee" before the "Player". Ensure 100% deterministic compliance checking.

- **Task 1.1: Nanosecond PCR Analyzer (`scripts/pcr_analyzer.py`) [DONE]**
  - Implemented using `scapy` with drift compensation and multi-format support.
- **Task 1.2: Containerized DVB Compliance Suite [DONE]**
  - Integrated with `TSDuck` via standard scripts.
- **Task 1.3: HAL Abstraction Layer [DONE]**
  - Defined `src/platform/hal.h` and implemented `hal_linux.c` and `hal_mock.c` with Virtual Time Domain support.

## 3. Phase 2: Core Logic & Standalone Validation [IN PROGRESS]
**Goal**: Verify mathematical models (PI Controller) and data structures (SPSC Queue) in isolation.

- **Task 2.1: Q16.16 Fixed-Point PI Controller [DONE]**
  - Implemented in `src/core/tstd_model.c` with anti-windup and deadband logic.
  - Verified stability in pacer clock compensation loop.
- **Task 2.2: Lock-Free SIMD Buffer [DONE]**
  - Implemented the SPSC queue with `alignas(128)` for false-sharing prevention.
  - Implemented 192-byte internal padding with `alignas(256)` for AVX2 zero-loop copies.
  - Applied C11 `memory_order_acquire/release` semantics.
- **Task 2.3: PCAPNG Mock Exporter [DONE]**
  - `hal_mock.c` generates nanosecond-precise PCAP files.
  - Fully automated via `make check-jitter`.

## 4. Phase 3: Real-Time Pacer & I/O Engine [IN PROGRESS]
**Goal**: Achieve low-jitter physical emission on Linux.

- **Task 3.1: Three-Stage Precision Pacer [DONE]**
  - Implemented `clock_nanosleep(TIMER_ABSTIME)` loop in `pacer_loop.c`.
  - Integrated PI controller for real-time jitter compensation.
- **Task 3.2: JIT Batching with `sendmmsg` [DONE]**
  - Implemented batched emission in `pacer_loop.c`.
  - Abstracted via `hal_ops.io_send`.

## 5. Phase 4: FFmpeg Ecosystem Integration [DESIGNED]
**Goal**: Full end-to-end professional encoding pipeline.

- **Task 4.1: AVIOContext Adapter [PLANNING]**
  - Architectural blueprint and "Hard Backpressure" mechanism documented in `docs/ffmpeg_integration.md`.
- **Task 4.2: Semantic Hinting Bypass [PLANNING]**
  - DPI logic for PCR/PSI detection documented in integration guide.
- **Task 4.3: End-to-End Field Test [PENDING]**

## 6. Success Criteria (Definition of Done)
- **Compliance**: Zero TR 101 290 errors reported by TSDuck.
- **Precision**: Max PCR Jitter < 30ns verified by Python Analyzer.
- **Performance**: Single-core throughput > 1 Gbps on standard x86_64.
- **Stability**: Zero packet loss or buffer overflow during a 24-hour "I-Frame Bomb" stress test.
