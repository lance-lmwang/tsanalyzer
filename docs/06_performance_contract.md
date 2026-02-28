# TsAnalyzer Performance Contract
## Phase 1 — Throughput as a Correctness Requirement

---

## 1. Contract Objective

In TsAnalyzer, performance is a **Correctness Requirement**, not an optimization target. If processing cannot sustain the ingress rate, temporal fidelity collapses, and measurement results become physically invalid.
**Throughput = Measurement Integrity.**

---

## 2. Fundamental Principle: Temporal Fidelity

Transport Stream analysis assumes that the **Observed Arrival Timeline** accurately reflects the **Physical Arrival Timeline**.
- Any internal packet loss or queue jitter distorts PCR deltas.
- Performance failure is classified as a **Metrology Failure**, invalidating derived jitter, VBV, and RST metrics.

---

## 3. Latency Budget Model (Maximum Internal Latency)

To ensure measurement stability, each packet experiences a strictly bounded analytical latency:

| Stage | Processing Budget | Requirement |
| :--- | :--- | :--- |
| **Capture** | < 5 µs | NIC to Ring Buffer. |
| **Decode** | < 15 µs | TS/PSI/SI decomposition. |
| **Metrology** | < 20 µs | VBV simulation & Math. |
| **Output** | < 10 µs | Serialization queue. |

Total pipeline latency SHALL remain bounded and monotonic. Latency variance (jitter) is treated as Timing Contamination.

---

## 4. Resource Determinism Requirements

### 4.1 NUMA & Cache Determinism
- **NUMA Locality**: All components (NIC, Threads, Memory) MUST remain on the same physical NUMA node.
- **Cache Alignment**: All descriptors SHALL be 64B cache-line aligned to eliminate false sharing.

### 4.2 Queue Stability Law
- **Steady-State**: Ingress Rate ≤ Processing Rate.
- **Forbidden**: Unbounded queue growth or backpressure that masks overload.
- **Burst Tolerance**: The engine MUST absorb a **150% bitrate burst for 50ms** without packet loss.

---

## 5. Packet Loss & Integrity Policy

Packet loss invalidates measurement continuity. Upon detecting `SO_RXQ_OVFL` or CC gaps:
1.  Record exact byte position of the loss.
2.  Mark the timing domain as **DEGRADED**.
3.  Suspend high-confidence predictive metrics (RST/VBV).
4.  Emit an **Integrity Warning**. **Silent continuation is forbidden.**

---

## 6. Sustained Operation Standards

The engine SHALL maintain performance stability during a **24h continuous line-rate execution**:
- **RSS Memory**: Zero allocator growth (flat-line).
- **Throughput**: 1.0 Gbps aggregate with zero drops.
- **CPU Variance**: < 0.1% variance in processing time.

---

## 7. Performance Failure Classification

| Condition | Metrology Impact |
| :--- | :--- |
| **Packet Drop** | Measurement Invalid |
| **Queue Drift** | Temporal Degradation |
| **NUMA Violation** | Timing Contamination |
| **Latency Spike** | Metrology Failure |

---

## 8. Non-Negotiable Rule

If sustained line-rate processing cannot be guaranteed under current host conditions, **TsAnalyzer MUST refuse to emit certification-grade measurements.** Incorrect certainty is prohibited.
