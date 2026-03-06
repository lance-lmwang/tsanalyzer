# TsAnalyzer: Determinism Threat Model (Phase 1)

To achieve bit-exact reproducibility and sub-microsecond precision, TsAnalyzer must identify and mitigate every source of non-determinism in the software stack.

---

## 1. Timing Entropy Sources

| Threat | Source | Mitigation Strategy |
| :--- | :--- | :--- |
| **System Clock Slewing** | NTP/PTP adjustments via `adjtimex`. | Use `CLOCK_MONOTONIC_RAW` for durations and NIC Hardware Timestamps for packet arrival. |
| **Leap Seconds** | OS wall-clock step. | Fully decouple metrology from UTC; use 27MHz linear STC reconstructed from PCR. |
| **Interrupt Latency** | Kernel IRQ handling jitter. | Leverage `SO_TIMESTAMPING` to capture arrival time at the NIC driver level, bypassing kernel scheduling. |

---

## 2. Memory & Throughput Entropy

| Threat | Source | Mitigation Strategy |
| :--- | :--- | :--- |
| **Cache Contention** | False sharing between capture and analysis threads. | Apply `alignas(64)` padding to packet descriptors and use SPSC lock-free patterns. |
| **NUMA Locality** | Cross-node memory access (QPI/UPI jitter). | Enforce NUMA-local memory allocation for Ring Buffers and pin processing threads to specific cores on that node. |
| **Heap Non-determinism** | `malloc/free` execution time variance and fragmentation. | Zero dynamic allocation in the fast path. All buffers and state structures are pre-allocated at startup. |

---

## 3. Mathematical & Logic Entropy

| Threat | Source | Mitigation Strategy |
| :--- | :--- | :--- |
| **Floating Point Drift** | IEEE 754 rounding differences across CPU architectures. | **Strict Prohibition**: No `float` or `double` in analysis paths. Use `stc_27m_t` (int64) for all timing and fixed-point math for bitrates. |
| **State Reset Races** | Asynchronous control commands (e.g., reset counters). | Implement "Command Synchronization Bits" in the stream metadata to ensure reset commands are executed at specific packet boundaries. |
| **Packet Reordering** | Network stack or multi-queue NIC reordering. | Layer 2 re-sequences packets based on hardware arrival timestamps before handing them to the metrology engine. |

---

## 4. Replay Determinism Contract

For an analysis result to be considered "Deterministic," the following **Causality Isolation** rules apply:
1.  **Input Invariance**: The same sequence of (Bytes, HW_Timestamp) must be provided.
2.  **Configuration Invariance**: Hash of all engine settings must match.
3.  **Engine Versioning**: Bit-identical code build.
4.  **Hardware Independence**: The result must be identical whether run on an 8-core or 128-core machine, as long as input/config match.

---

## 5. Verification Gate
*   **Chaos Testing**: Inject artificial CPU load and IRQ storms while replaying a PCAP.
*   **Success**: Output JSON hash remains unchanged regardless of system stress level.
