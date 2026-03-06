# Metrology Core: Implementation Logic

Layer 3 implementation details for numerical stability and deterministic execution.

## 1. Numerical Stability

### 1.1 Fixed-Point Mastery
To ensure platform independence and prevent drift, all analysis paths use **__int128 Q64.64 fixed-point math**.
*   Bitrate calculation: $Rate = \frac{\Delta Bytes \times 8 \times 2^{64}}{\Delta VSTC}$
*   Jitter: Calculated in 27MHz ticks, converted to nanoseconds only at the final reporting stage.

### 1.2 Kahan Compensated Summation
For long-term drift estimation, the engine employs the Kahan algorithm to maintain precision when adding small deltas to large accumulators:
```c
double y = input - c;
double t = sum + y;
c = (t - sum) - y;
sum = t;
```

---

## 2. Synchronized Metrology Barrier

To prevent **Bitrate Inversion** (where a snapshot shows more bits arriving than time elapsed), the engine implements a global sampling barrier.
1.  **Block**: All worker threads hit a spin-lock barrier.
2.  **Sync**: All counters and the VSTC reference are cloned into a "Sampling Plane."
3.  **Release**: Workers resume analysis.
4.  **Compute**: The reporting thread calculates deltas from the Sampling Plane.

---

## 3. Determinism Guardrails

*   **No Malloc**: Memory pools are allocated at startup.
*   **No CLOCK_REALTIME**: The engine is "blind" to the wall-clock; it only sees HAT and PCR.
*   **Thread Isolation**: Core-masking prevents cross-core IRQ pollution.
