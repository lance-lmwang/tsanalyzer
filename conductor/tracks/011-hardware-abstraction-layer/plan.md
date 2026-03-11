# Implementation Plan: Hardware Abstraction Layer (HAL)

## Status
- [x] **Phase 1: Refactor Source Structure**
  - [x] Extract core SIMD logic into `src/tsa_simd.c` and `src/tsa_simd_avx2.c`.
  - [x] Define the `tsa_simd_ops_t` vtable in `include/tsa_simd.h`.

- [x] **Phase 2: Implement Generic C99 Fallback**
  - [x] Developed scalar fallback in `src/tsa_simd.c` using standard loop logic.
  - [x] Verified accuracy against SIMD version.

- [x] **Phase 3: Runtime Hardware Detection**
  - [x] Implemented `tsa_simd_init()` using `__get_cpuid`.
  - [x] Implemented constructor-based auto-initialization for the vtable.

- [ ] **Phase 4: SSE4.2 Optimization & Benchmarking**
  - [ ] Implement `src/tsa_simd_sse42.c` using `_mm_cmpestri` for sync search.
  - [ ] Update dispatcher to support SSE4.2 fallback.
  - [ ] Compare throughput across different CPU profiles.

## Completion Criteria
1.  **Broad Compatibility**: The system starts and analyzes streams on any X86_64 CPU without crashing on illegal instructions.
2.  **Accuracy**: Zero difference in TR 101 290 and PCR results between different HAL implementations.
3.  **Stability**: Long-term (1h) stress test on a generic VM (no SIMD) with stable metrics.
