# Resource & Performance Spec: High-Fidelity Metrology

TsAnalyzer is optimized for **Deterministic Metrology** at 1Gbps line-rate. The performance goal is to maintain absolute precision while analyzing 1.2 Million packets per second (PPS) on a single physical core.

---

## 1. Single-Node Performance Targets

*Hardware Baseline: 16-Core Node (e.g., AWS c6g.4xlarge or equivalent x86_64)*

| Metric | Phase 1 Target | Implementation Detail |
| :--- | :--- | :--- |
| **Max Throughput** | 1.5 Gbps | Sustained aggregate analysis on a single NUMA node. |
| **PPS (Packets/Sec)** | 1.2M PPS | Peak per-core processing limit with full TR 101 290 coverage. |
| **Packet Latency** | < 800ns | Per-packet internal processing time (Ingress to Analysis). |
| **Jitter Margin** | ≤ ±10ns | Measurement error relative to hardware reference (NIC HW TS mode). |

### 1.1 Linearity & Scaling
Throughput scales linearly with the number of physical cores. By pinning one stream per core, TsAnalyzer can handle up to **12.8 Gbps** on a 16-core machine while maintaining deterministic output for every stream.

---

## 2. Resource Determinism Model

To guarantee **Temporal Fidelity**, TsAnalyzer enforces strict resource boundaries:

*   **Memory Footprint**: Strictly bounded at **64MB per HD stream**. All buffers are pre-allocated at initialization to prevent `malloc` variance.
*   **CPU Residency**: Analysis threads are hard-pinned to isolated cores (`isolcpus`).
*   **Memory Wall Consistency**: Ring buffers are allocated using **Static Huge Pages** to minimize TLB misses and ensure consistent memory access latency.

---

## 3. Performance Success Criteria (Gates)

*   **Gate G1 (Throughput)**: 1Gbps aggregate throughput with zero kernel-level drops (`SO_RXQ_OVFL` = 0).
*   **Gate G2 (Precision)**: Sustained ±10ns PCR accuracy under 1.2M PPS load.
*   **Gate G3 (Stability)**: 24h continuous operation at line-rate with < 0.1% CPU variance.
*   **Gate G4 (Determinism)**: Binary identical JSON output for identical PCAP input across varying system loads.
