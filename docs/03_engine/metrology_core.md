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

## 3. Lock-Free Data Plane

The fast path from ingestion to analysis is 100% wait-free.

### 3.1 Packet Descriptor
Every packet is represented by a small descriptor to keep the SPSC ring compact:
```c
typedef struct {
    uint64_t hw_timestamp; // NIC-level nanoseconds
    uint32_t flow_id;      // Unique stream identifier
    uint16_t len;          // 188 for TS
    uint8_t *data;         // Buffer pointer (NUMA-local)
} tsa_packet_t;
```

### 3.2 SPSC Ring Structure
Utilizes cache-line padding and C11 atomics:
```c
typedef struct {
    alignas(64) _Atomic uint64_t head;
    alignas(64) _Atomic uint64_t tail;
    tsa_packet_t slots[4096];
} tsa_ring_t;
```
*   **Producer (RX Worker)**: `store_release(head)` after writing packet.
*   **Consumer (Reactor)**: `load_acquire(head)` before reading.

---

## 4. Determinism Guardrails

*   **No Malloc**: Memory pools are allocated at startup.
*   **No CLOCK_REALTIME**: The engine is "blind" to the wall-clock; it only sees HAT and PCR.
*   **Thread Isolation**: Core-masking prevents cross-core IRQ pollution.
