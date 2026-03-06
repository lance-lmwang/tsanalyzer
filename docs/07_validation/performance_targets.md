# Performance Targets & Success Gates

TsAnalyzer v3 follows a strict performance contract where throughput is a correctness requirement.

## 1. Performance Targets (Per Node)

| Target | Specification | Condition |
| :--- | :--- | :--- |
| **Max Throughput** | 1.5 Gbps | Sustained, no loss. |
| **Packet Density** | 1.2M PPS | Per physical core. |
| **Analysis Latency**| < 800ns | Ingress to Metrology. |
| **Measurement Jitter**| ≤ ±10ns | Relative to hardware. |

---

## 2. Engineering Verification Gates

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
