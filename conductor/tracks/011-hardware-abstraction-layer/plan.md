# Implementation Plan: Hardware Abstraction Layer (HAL)

## Status
- [ ] **Phase 1: Refactor Source Structure**
  - [ ] Extract current AVX-512 TS parsing logic from `src/tsa_parser.c` to `src/tsa_parser_avx512.c`.
  - [ ] Define the `tsa_parser_ops_t` vtable in `include/tsa_internal.h`.

- [ ] **Phase 2: Implement Generic C99 Fallback**
  - [ ] Develop `src/tsa_parser_generic.c` using standard C logic (loop-unrolled `memchr`-style search).
  - [ ] Verify byte-by-byte accuracy against the SIMD version.

- [ ] **Phase 3: Runtime Hardware Detection**
  - [ ] Implement `tsa_cpu_init()` using `cpuid` (X86) or `/proc/cpuinfo` (ARM).
  - [ ] Implement the initialization-time vtable linkage (e.g., `current_ops = tsa_detect_best_parser()`).

- [ ] **Phase 4: Optimization & Benchmarking**
  - [ ] Implement `AVX2` and `SSE4.2` specific versions.
  - [ ] Compare throughput across different CPU profiles (Docker-limited CPU flags).
  - [ ] Profile the function-pointer call overhead and consider inlining strategies (e.g., LTO).

## Completion Criteria
1.  **Broad Compatibility**: The system starts and analyzes streams on any X86_64 CPU without crashing on illegal instructions.
2.  **Accuracy**: Zero difference in TR 101 290 and PCR results between different HAL implementations.
3.  **Stability**: Long-term (1h) stress test on a generic VM (no SIMD) with stable metrics.
