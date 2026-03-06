# TsAnalyzer: Performance Contracts & Verification Matrix

This document defines the non-negotiable performance standards, architectural constraints, and the rigorous verification matrix required to certify TsAnalyzer as a "Broadcast-Grade" instrument.

---

## 1. Non-Negotiable Architectural Constraints

To preserve TsAnalyzer as a deterministic measurement instrument, the following rules are permanent engineering laws:

1.  **Execution Determinism**: Analytical output must depend exclusively on input content, arrival order, and engine version. Replay result must equal live capture result bit-for-bit.
2.  **Temporal Integrity**: No use of `CLOCK_REALTIME` or system wall-clock for metrology. Timing derives only from NIC Hardware Timestamps (HAT) and reconstructed PCR (STC).
3.  **Zero-Copy Memory Wall**: Zero dynamic allocation (`malloc/free`) in the fast path. Packets move via linear pointer transfer; no internal packet copying allowed.
4.  **No Silent Recovery**: The engine SHALL NEVER conceal packet loss, interpolate missing segments, or guess timestamps. Integrity supersedes "looking good."

---

## 2. Performance & Resource Contracts

TsAnalyzer treats throughput as a **Metrology Correctness Requirement**.

### 2.1 Single-Node Targets
*   **Throughput**: 1.5 Gbps sustained aggregate per NUMA node.
*   **Density**: 1.2M PPS per physical core with 100% TR 101 290 coverage.
*   **Latency**: Ingress-to-Analysis latency < 800ns.
*   **Jitter Margin**: Error relative to hardware reference ≤ ±10ns.

### 2.2 Processing Latency Budget (Per Packet)
| Stage | Budget | Requirement |
| :--- | :--- | :--- |
| **Capture** | < 5 µs | NIC to Ring Buffer. |
| **Decode** | < 15 µs | TS/PSI/SI decomposition. |
| **Metrology** | < 20 µs | VBV simulation & Math. |
| **Output** | < 10 µs | Serialization queue. |

---

## 3. The Determinism Threat Model

| Threat | Mitigation Strategy |
| :--- | :--- |
| **Clock Slewing** | Use `CLOCK_MONOTONIC_RAW` and Hardware Timestamps. |
| **NUMA Jitter** | Enforce NUMA-local memory and thread affinity pinning. |
| **Floating Point Drift**| **Forbidden**. Use `int128` and fixed-point math (Q64.64) for all timings. |
| **Heap Variance** | Pre-allocate all buffers and state structures at startup. |

---

## 4. Verification Matrix (Success Gates)

### 4.1 Gate G1: Throughput & Loss
*   **Test**: 1.2M PPS CBR UDP for 60 minutes.
*   **Success**: `SO_RXQ_OVFL` = 0 and 0 CC errors.

### 4.2 Gate G2: Metrology Accuracy
*   **Test**: Compare PCR Jitter against Tektronix MTS4000 or Sencore VideoBRIDGE.
*   **Success**: Deviation ≤ ±10ns.

### 4.3 Gate G3: Execution Determinism
*   **Test**: Run the same PCAP 100 times under varying system stress.
*   **Success**: 100% bit-identical JSON output hashes.

### 4.4 Gate G4: 24h Stability (Soak Test)
*   **Test**: Continuous line-rate execution for 24 hours.
*   **Success**: RSS memory remains flat; CPU variance < 0.1%.

---

## 5. Implementation Roadmap

### Phase 1: Foundation (COMPLETED)
*   Hardware timestamping, high-precision Software PLL, and zero-copy SPSC pipeline.

### Phase 2: Structural Depth (COMPLETED)
*   Full TR 101 290 P1/P2, SCTE-35 auditing, and Annex D T-STD simulation.

### Phase 3: Content Intelligence (IN PROGRESS)
*   Zero-Copy NALU Sniffer (H.264/HEVC), GOP Tracking, and Predictive RST.

### Phase 4: Hybrid Scale (PLANNED)
*   I/O-Computing decoupling via Reactor model to support 500+ concurrent streams.
