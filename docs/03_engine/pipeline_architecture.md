# High-Throughput Lock-Free TS Pipeline

TsAnalyzer Pro is designed to sustain **8+ million TS packets per second (pps)** on commodity servers while maintaining deterministic latency. At 188 bytes per packet, this equates to a theoretical data rate of approximately **12 Gbps**.

---

## 1. Design Objective

To reach this level of performance, the system eliminates:
*   **Kernel scheduling overhead** via dedicated worker affinity.
*   **Lock contention** via wait-free primitives.
*   **Memory copies** via pointer passing.
*   **Cache line bouncing** via cache-aligned structures.

---

## 2. Pipeline Overview

The data plane follows a **Run-to-Completion** streaming architecture.

```text
NIC RX (AF_XDP/DPDK)
        │
        ▼
+----------------+
| Ingestion Stage| (Zero-copy DMA to Hugepages)
+----------------+
        │
        ▼
+----------------+
| SIMD TS Parser | (Vectorized Sync/PID extraction)
+----------------+
        │
        ▼
+----------------+
| Stream Demux   | (SPSC Handoff / PID Routing)
+----------------+
        │
        ▼
+----------------+
| Timing Engine  | (PCR Software PLL / Metrology)
+----------------+
        │
        ▼
+----------------+
| Metric Export  | (Atomic Snapshot to Metrics Bus)
+----------------+
```

---

## 3. Lock-Free SPSC Ring Buffer

The core primitive is a **Single-Producer Single-Consumer (SPSC)** ring buffer utilizing C11 Acquire/Release semantics.

### 3.1 Memory Layout
```c
struct PacketRef {
    uint64_t timestamp;
    uint16_t length;
    uint8_t* data_ptr;
};

typedef struct {
    alignas(128) _Atomic uint64_t head; // Producer index
    alignas(128) _Atomic uint64_t tail; // Consumer index
    struct PacketRef slots[RING_SIZE];
} tsa_spsc_ring_t;
```

### 3.2 Operation
*   **Producer**: Calculates `head % size`, writes the `PacketRef`, and performs an `atomic_store_explicit(..., memory_order_release)`.
*   **Consumer**: Performs an `atomic_load_explicit(..., memory_order_acquire)`, reads the `PacketRef`, and increments the local tail.
*   **Latency**: Measured at **< 50 ns** per handoff.

---

## 4. Zero-Copy Packet Flow

TsAnalyzer uses **Pointer Passing** instead of data copying.
1.  **NIC DMA**: Writes packets directly into a **Hugepage Memory Pool**.
2.  **Pipeline**: Only the `PacketRef` (16-24 bytes) is moved between stages.
3.  **Immutability**: Packet memory remains immutable throughout the analytical lifecycle.
4.  **Copy Count**: **Strictly 0**.

---

## 5. Cache-Aligned Batching

The engine processes packets in batches (typically **32 packets**) to maximize CPU efficiency.
*   **SIMD Efficiency**: Vectors are loaded once and processed in registers.
*   **Prefetching**: The CPU prefetcher easily predicts the linear memory access of the hugepage pool.
*   **Cache Locality**: Multiple `PacketRef` descriptors fit within a single L1 cache line.

---

## 6. NUMA-Aware Stream Sharding

Large deployments distribute streams across physical CPU sockets to eliminate interconnect latency.
*   **Rule**: A stream must not cross the NUMA boundary.
*   **Scaling**: Throughput scales linearly with the number of NUMA nodes.

| Cores | Max Streams | Target Throughput |
| :--- | :--- | :--- |
| **16** | 256 | ~2 Gbps |
| **32** | 512 | ~4 Gbps |
| **64** | 1000 | ~8-10 Gbps |

---

## 7. Soft Backpressure Control

If the metrology analyzer falls behind, the parser monitors ring occupancy.
*   **Threshold**: At **80% occupancy**, the parser reduces the batch fetch size (e.g., 32 → 16 → 8).
*   **Result**: Stabilizes the pipeline latency and prevents packet drops during transient CPU spikes.
