# TsAnalyzer: Lock-Free Data Plane (Design Deduction)

To achieve **8M pps** (one packet every 125ns), the handoff between the Ingest RX worker and the Metrology Reactor must be zero-latency. This document deduces the memory barrier and cache-line design for the SPSC (Single-Producer Single-Consumer) Ring Buffer.

---

## 1. Physical Layout: Cache Line Isolation

Modern high-end CPUs (AMD Zen 4/5, Intel Sapphire Rapids) employ aggressive prefetching. To prevent **False Sharing**, we must isolate the Producer and Consumer cursors.

### 1.1 alignas(128)
While standard cache lines are 64 bytes, adjacent cache line prefetchers can cause two 64-byte lines to be pulled together. We use `alignas(128)` to ensure absolute isolation.

```c
typedef struct {
    alignas(128) _Atomic uint64_t head; // Written by Producer
    alignas(128) _Atomic uint64_t tail; // Written by Consumer

    // Packet slots (Token array)
    tsa_packet_t slots[RING_SIZE];
} tsa_v3_ring_t;
```

---

## 2. Memory Barrier Strategy (Acquire/Release)

We abandon `memory_order_seq_cst` (default) which issues heavy "lock" prefixed instructions or full memory fences. Instead, we use the **C11 Acquire/Release Semantics**.

### 2.1 The Producer (RX Worker)
The producer writes the data first, then "releases" the cursor.
```c
// 1. Write packet data to NUMA-local slot
tsa_v3_write_packet(&ring->slots[idx], raw_pkt);

// 2. Publish the change
// This ensures all previous writes (the packet data) are visible
// to any thread that performs an 'acquire' on this head.
atomic_store_explicit(&ring->head, new_head, memory_order_release);
```

### 2.2 The Consumer (Reactor Worker)
The consumer "acquires" the cursor, ensuring it sees the data written by the producer.
```c
// 1. Poll the head
// This 'acquire' creates a happens-before relationship with the producer's 'release'.
uint64_t current_head = atomic_load_explicit(&ring->head, memory_order_acquire);

// 2. Process batch
if (current_head > local_tail) {
    // We are guaranteed to see the bit-exact packet data here
    process_packet(&ring->slots[local_tail % RING_SIZE]);
}
```

---

## 3. Performance Deduction

| Operation | Overhear (Cycles) | Impact at 8M pps |
| :--- | :--- | :--- |
| **Atomic Seq_Cst** | ~100-200 | **CRITICAL**: Would consume the entire 125ns budget. |
| **Acquire/Release** | ~10-30 | **OPTIMAL**: Leaves ~100ns for TR 101 290 and Math. |
| **False Sharing** | ~500+ | **FATAL**: Pipeline stalls; packet drops inevitable. |

---

## 4. Implementation Strategy

1.  **NUMA Locality**: The `slots` array and the packet data buffers must be allocated using `mmap` with `MAP_HUGETLB` on the same NUMA node as the consumer thread.
2.  **Batch Handoff**: The consumer will not process one packet at a time but will "grab" all available packets up to the current `head` in one cache-friendly sweep.
