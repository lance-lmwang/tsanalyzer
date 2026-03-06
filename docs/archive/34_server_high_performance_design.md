# TsAnalyzer: High-Performance Multi-Channel Metrology Server

## 1. Hardware Topology & Determinism (The Bare-Metal Foundation)

To achieve bit-exact reproducibility and ±10ns precision, the server is treated as a **Real-Time Measurement Instrument**.

#### 1.1 CPU & Spatial Isolation
*   **NUMA-local Data Plane**: Worker threads, NIC IRQs, and Ring Buffers MUST reside on the same NUMA node to eliminate QPI/UPI jitter.
*   **Temporal Fidelity Fencing**: `isolcpus` and `rcu_nocbs` are used to shield metrology threads from kernel scheduling variance.

#### 1.2 Memory Wall Consistency
*   **Static Huge Pages**: Ring buffers use pre-allocated 2MB huge pages to eliminate TLB shootdown risks and ensure constant memory access latency during line-rate ingestion.

---

## 2. Ingest Integrity (Zero-Jitter Pipeline)

#### 2.1 Hardware Timestamp Mandate
*   **Implementation**: `SOF_TIMESTAMPING_RX_HARDWARE`. Extracts PHY-level nanosecond timestamps to feed the **V-STC reconstruction engine**. This bypasses all OS-layer timing entropy.

---

## 3. Data Plane: L2-Bound Metrology

To ensure deterministic 1.2M PPS processing, the **V-STC and TR 101 290 FSMs** are optimized for L2 cache residency.

*   **Zero-Copy SPSC**: Packets stay in the NUMA-local Ring Buffer. Analysis threads access descriptors via cache-line aligned pointers.
*   **Branchless Logic**: Metrology paths use branchless state transitions to prevent non-deterministic CPU pipeline flushes.

---

## 4. Precision Metrology Math

#### 4.1 V-STC & Error Compensation
Standard `double` precision is insufficient for 27MHz measurements.
*   **Kahan Compensated Summation**: Used in long-term drift estimation to prevent floating-point loss of precision.
*   **Fixed-Point V-STC**: All internal timebases use 64-bit integer `stc_27m_t` to ensure bit-identical results regardless of the host's floating-point unit (FPU) state.

#### 4.2 Replay Determinism Contract
Every Bare-metal optimization described here serves one goal: **Execution Determinism**. Replaying the same bitstream through this hardened stack MUST yield the identical MD5-consistent JSON report, independent of background system load.
