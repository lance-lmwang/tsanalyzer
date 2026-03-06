# TsAnalyzer v3: SIMD Vectorized Parser (Design Draft)

To achieve dimensional reduction in processing speed, TsAnalyzer v3 replaces the legacy bit-by-bit state machine with a **Vectorized SIMD Parser** targeting AVX-512 (with AVX2 fallback).

---

## 1. Vectorized Alignment & Sync Detection

A standard TS packet is 188 bytes.
*   **AVX-512**: 64 bytes per register. 3 registers cover ~192 bytes (nearly 1 packet).
*   **AVX2**: 32 bytes per register. 6 registers cover ~192 bytes.

### 1.1 Fast Sync Search
Using `_mm512_cmpeq_epi8_mask`, we can verify the `0x47` sync byte at the head of multiple packets in a single cycle.

```c
// Scan 64 packets simultaneously if stored in a transposed/interleaved buffer
__m512i sync_pattern = _mm512_set1_epi8(0x47);
__mmask64 mask = _mm512_cmpeq_epi8_mask(batch_heads, sync_pattern);

if (mask != 0xFFFFFFFFFFFFFFFF) {
    // Immediate detection of sync-loss in the batch
}
```

---

## 2. Header Extraction & Masking

The TS header occupies the first 4 bytes.

### 2.1 13-bit PID Gathering
Instead of manual bit-shifting per packet, we use **Gather** instructions to pull the first 32-bits of multiple TS packets into a vector.

1.  **Gather**: `_mm512_i32gather_epi32` (collects headers of 16 packets).
2.  **Permute/Shuffle**: Align the PID bits.
3.  **AND Mask**: `PID = header & 0x1FFF0000` (shifted).
4.  **Compare & Drop**:
    ```c
    __m512i pids = ...;
    __m512i null_pid = _mm512_set1_epi32(0x1FFF);
    __mmask16 drop_mask = _mm512_cmpeq_epi32_mask(pids, null_pid);
    // Use drop_mask to skip processing or zero out payloads
    ```

---

## 3. Register-Level Continuity Check

We can perform CC sequence validation for a batch of packets belonging to the same PID using vector subtraction.

```c
// Assuming 'expected_ccs' and 'actual_ccs' vectors
__m512i diff = _mm512_sub_epi32(actual_ccs, expected_ccs);
__mmask16 error_mask = _mm512_cmpneq_epi32_mask(diff, _mm512_setzero_si512());
```

---

## 4. Architectural Goals

1.  **Instruction Density**: Aim for < 10 SIMD instructions per packet for the entire Header + CC + Adaptation Field detection.
2.  **Branchless Execution**: Use **Masked Stores** and **Masked Loads** (`_mm512_mask_mov_epi8`) to handle optional fields like Adaptation Fields without `if/else` branching.
3.  **Cache Locality**: Process packets in batches of 16 (AVX-512) or 8 (AVX2) to stay within the L1 Instruction Cache.

---

## 5. Deployment Recommendation

*   **Primary**: AVX-512 (Intel Ice Lake/Sapphire Rapids, AMD Zen 4).
*   **Fallback**: AVX2 + BMI2 (standard x86_64 servers). The logic remains identical but with 256-bit granularity.
