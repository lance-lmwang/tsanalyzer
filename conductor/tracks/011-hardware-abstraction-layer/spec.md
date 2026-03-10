# Specification: Hardware Abstraction Layer (HAL)

## 1. Objective
Enable TsAnalyzer Pro to run on diverse CPU architectures (X86_64 with/without AVX-512, ARMv8/v9) by abstracting the TS parsing and metrology logic away from specific SIMD instruction sets.

## 2. Requirements
- **Runtime Dispatching**: Detect CPU flags (AVX-512, AVX2, SSE4.2) at startup and link the optimal implementation.
- **Generic Fallback**: Provide a standard C99 implementation for environments lacking SIMD optimization.
- **Functional Parity**: The Generic path MUST pass the exact same compliance tests as the AVX-512 path.
- **Minimal Overhead**: The abstraction layer (e.g., function pointers or jump tables) must not introduce more than 1-2% CPU overhead.

## 3. Architecture: The Parser VTable
We will introduce a `tsa_parser_ops_t` structure containing function pointers for hot-path operations:
- `find_sync_byte()`: Scans buffer for 0x47.
- `extract_header()`: Parses 4-byte TS header into structured metadata.
- `payload_copy()`: Optimized memory movement for PES/ES extraction.

## 4. Performance Scaling
| Implementation | Expected PPS (per core) | Target Environment |
| :--- | :--- | :--- |
| **AVX-512** | 8.0M+ | High-end Xeon/EPYC |
| **AVX2** | 5.0M | Mid-range Server |
| **SSE4.2** | 3.0M | Legacy/VM |
| **Generic C** | 1.5M | General Cloud Instance |
