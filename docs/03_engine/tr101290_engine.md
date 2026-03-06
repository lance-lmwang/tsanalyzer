# TR 101 290 Real-Time Monitoring Engine

The TsAnalyzer Engine employs an event-driven, **$O(1)$ complexity monitoring model** designed to maintain sub-millisecond detection latency and stable CPU utilization across 1000+ concurrent Transport Streams.

---

## 1. Design Goals

To achieve industrial-scale monitoring, the engine eliminates periodic scanning ("polling") of streams.
*   **Scalability**: Support 1000+ streams on a single appliance.
*   **Latency**: Detect Priority 1 faults within a single packet duration.
*   **Efficiency**: Maintain a constant CPU instruction budget per packet (~20 cycles for core checks).

---

## 2. $O(1)$ Event-Driven Model

Instead of a background task that iterates through streams, every incoming TS packet triggers a specific set of checks based on its PID and metadata.

### 2.1 Pipeline Integration
1.  **PID Classification**: Instant lookup of per-PID state.
2.  **State Update**: Incrementing counters and updating timestamps in pre-allocated memory.
3.  **Boundary Check**: Verifying values against ETSI thresholds (e.g., CC sequence, Sync byte).

---

## 3. Time-Wheel Scheduler

Repetition-based checks (PAT/PMT every 500ms, PCR every 40ms) are notoriously expensive to monitor using standard OS timers. TsAnalyzer utilizes a **Hierarchical Time-Wheel** to manage thousands of concurrent watchdogs with $O(1)$ overhead.

### 3.1 Structure
*   **Wheel Size**: 1024 slots.
*   **Resolution**: 10ms per slot.
*   **Range**: ~10.24 seconds of look-ahead.

### 3.2 Logic
*   **Insertion**: When a packet arrives, the "Next Expected" deadline is calculated and the Stream ID is appended to the corresponding future slot:
    $$slot = (current\_slot + \frac{interval}{resolution}) \pmod{wheel\_size}$$
*   **Execution**: On every wheel tick, the engine only processes the list of streams in the current slot. This guarantees that the cost of monitoring repetitions is independent of the total number of monitored streams.

---

## 4. Stream State Machine (FSM)

Each stream is treated as a state machine that matures as protocol layers are discovered:
`INIT` → `PAT_FOUND` → `PMT_FOUND` → `PCR_LOCKED` → `RUNNING`

Transitions occur only on relevant packet arrivals, eliminating the need for expensive "table discovery" scans in the hot path.

---

## 5. Optimized Detection Algorithms

| Operation | Complexity | CPU Cycles (Est.) | Description |
| :--- | :--- | :--- | :--- |
| **Sync Check** | $O(1)$ | ~1 | Single byte comparison (`0x47`). |
| **PID Lookup** | $O(1)$ | ~3 | Direct array indexing per-stream. |
| **CC Sequence**| $O(1)$ | ~4 | Arithmetic check with Discontinuity support. |
| **State Update**| $O(1)$ | ~5 | Updating timestamps and byte offsets. |
| **Total Core** | **$O(1)$** | **~20 cycles** | **Total cost per packet.** |

---

## 6. Scaling & Cache Optimization

### 6.1 Memory Footprint
Per-stream state is kept under **4 KB** to ensure that the metadata for 1000 streams (totaling ~4 MB) fits entirely within the CPU's **L3 Cache**. This prevents expensive DRAM round-trips during the analysis loop.

### 6.2 Lock-Free Export
Metrology results are pushed to an asynchronous **Metrics Bus** via lock-free MPMC queues. The Data Plane never blocks on JSON serialization or HTTP I/O.

---

## 7. Fault Correlation

The engine performs real-time causal analysis by correlating low-level events:
*   **Case**: `PCR Jitter Spike` + `BSM % Drop` → **Network Micro-burst** diagnosis.
*   **Case**: `CC Error` + `GOP Discontinuity` → **Signal Loss** diagnosis.
*   **Case**: `Stable Link` + `Entropy Variance -> 0` → **Encoder Freeze** diagnosis.
