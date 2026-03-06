# Performance Targets & Success Gates

TsAnalyzer v3 follows a strict performance contract where throughput is a correctness requirement.

## 1. Scaling Model & Performance Estimates

The system is designed to scale from laboratory probes to ISP-scale monitoring appliances.

| Capability | 1 Gbps Profile | 10 Gbps Profile |
| :--- | :--- | :--- |
| **RX Threads** | 1 | 8 |
| **Reactor Nodes** | 2 | 8 |
| **RSS Mapping** | Queue 0-1 | Queue 0-7 |
| **Throughput** | ~830k PPS | ~8.3M PPS |
| **Latency** | < 2ms | < 2ms |

### 1.1 Efficiency Metrics
*   **TS Analysis Cost**: ~250 CPU cycles per packet.
*   **Cycle Budget**: A 10Gbps stream (8.3M PPS) requires approximately 2 billion cycles per second, equivalent to **1 modern physical CPU core** for the entire aggregate analysis logic.

---

## 2. Performance Targets (Per Core)

Before a build is certified for production, it must pass these automated gates:

### Gate G1: Line-Rate Stress
*   **Test**: 1.2M PPS CBR UDP for 60 minutes.
*   **Pass**: `SO_RXQ_OVFL` == 0 and TR 101 290 P1 Errors == 0.

### Gate G2: Mathematical Determinism
*   **Test**: Run the same 10-minute PCAP 100 times under varying background load.
*   **Pass**: Every JSON output hash must be identical.

### Gate G3: Instrument Accuracy
*   **Test**: Compare Jitter/RST results against Tektronix MTS4000 hardware.
*   **Pass**: Deviation ≤ 2%.

### Gate G4: 24h Resilience
*   **Test**: 24-hour soak test at 1Gbps.
*   **Pass**: Flat RSS memory line; zero CPU spikes.
