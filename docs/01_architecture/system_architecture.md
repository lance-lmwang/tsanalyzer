# TsAnalyzer Pro: v3 System Architecture

TsAnalyzer v3 is not a traditional server application, but a **Deterministic Dataflow Machine**. Its primary mission is to transform raw NIC packets into high-fidelity metrology with hardware-level precision.

---

## 1. Core Design Philosophy

1.  **NUMA Locality First**: Packet data and processing state must never cross physical CPU socket boundaries (zero QPI/UPI latency).
2.  **Lock-Free Data Plane**: 0 mutex, 0 malloc, 0 blocking in the fast path.
3.  **Batch Processing Everywhere**: Utilize `recvmmsg` and vector instructions to process packets in groups.
4.  **Streams are State Machines**: A stream is not a thread; it is a compact state machine that must fit within the CPU's L1/L2 cache.
5.  **Branchless Logic**: Eliminate conditional branches in the TS parser to maximize pipeline efficiency.

---

## 2. Global System Architecture

The architecture utilizes a tiered fan-out model to scale to **10 Gbps** and **500+ streams**:

```text
            NIC (10G / 40G)
                 │
          recvmmsg Ingest (Batch)
                 │
           RX Worker (Per-Queue)
                 │
            Flow Hash (RSS-like)
                 │
   ┌─────────────┼─────────────┐
   │             │             │
Reactor 0      Reactor 1      Reactor N
 100 streams    100 streams    100 streams
   │             │             │
Lock-Free SPSC Rings (Wait-free handoff)
   │
TS Analysis Core (Layer 2 & 3)
   │
Metrics Bus (Atomic Snapshots)
   │
Prometheus / tsa_top / JSON
```

---

## 3. The NUMA Pipeline

NUMA awareness is the difference between a tool and an instrument at 10 Gbps.

### 3.1 Data Residency Rule
**Packet data must never cross NUMA nodes.** Crossing the interconnect (QPI/UPI) introduces non-deterministic latency spikes that pollute jitter measurements.

### 3.2 Thread-to-Core Mapping
*   **Ingest RX Worker**: Pinned to the same NUMA node as the NIC PCIe lane.
*   **Reactor Workers**: Pinned to physical cores adjacent to the RX Worker.
*   **Metrics Reporting**: Low-priority background threads on non-isolated management cores.

---

## 4. Reactor Stream Model

Unlike v2 which used "Thread-per-Stream," v3 treats **Streams as State Machines** multiplexed inside a fixed number of **Reactor Threads**.

### 4.1 Event Loop Logic
Each Reactor manages ~100 streams. The loop performs wait-free polling:
```c
while(running) {
    batch = ring_pop_batch();
    for(pkt in batch) {
        stream = flow_table[pkt.flow];
        process_ts_packet(stream, pkt); // Updates state machines
    }
}
```

### 4.2 Cache Residency
The `ts_stream_t` context is carefully packed to reside within a single cache line (64-128 bytes) where possible, ensuring that all metrology math (PCR PLL, VBV) happens in L1/L2 cache.
