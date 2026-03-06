# TsAnalyzer Pro: Technical Vision

TsAnalyzer Pro is engineered to provide a **software-defined alternative** to proprietary hardware analyzers. It achieves hardware-level speed and precision through a deterministic architecture optimized for high-performance commodity servers.

## Core Mission
To maximize the utilization of modern multi-core CPUs and SIMD instruction sets. Utilizing a lock-free, zero-copy architecture, TsAnalyzer Pro serves as an industrial-grade metrology platform capable of sustaining massive throughput and nanosecond-level precision for critical broadcast and streaming infrastructure.

---

## The Four Pillars of TsAnalyzer Pro

### 1. Performance at the Edge of Silicon
TsAnalyzer Pro bypasses the limitations of traditional state machines by leveraging **SIMD (AVX2 / AVX-512)** for dimensional reduction.
*   **Vectorized Parsing**: Single-cycle batch validation and PID filtering of 188-byte packets.
*   **End-to-End Zero-Copy**: An 8M pps (10Gbps+) lock-free pipeline utilizing kernel-bypass (XDP/DPDK) to eliminate context switching and memory bottlenecks.

### 2. Nanosecond-Level Metrology
In professional broadcasting, timing is the ultimate truth. TsAnalyzer Pro functions as a high-precision digital vernier caliper.
*   **High-Fidelity Clock Modeling**: Moving beyond system-level timestamps to hardware-level NIC PTP/HAT integration for absolute PCR Jitter and Drift metrology.
*   **Standard Compliance**: Built-in O(1) time-wheel algorithms ensure TR 101 290 compliance with microsecond-level accuracy across 1000+ concurrent streams.

### 3. Massive Scalability & Resilient Architecture
*   **NUMA-Aware Data Plane**: Ensuring all data processing and memory residency remains local to the physical CPU socket to eliminate QPI/UPI latency.
*   **Run-to-Completion Model**: Replaces traditional multi-threading with an asynchronous event-driven mechanism to maximize cache efficiency and CPU utilization.

### 4. Deep Semantic Inspection
*   **Service Inventory & CAS Audit**: Automated mapping of PIDs to friendly channel names (SDT) and provider IDs. Monitoring of Conditional Access (CA) encryption states and CAID compliance.
*   **StatMux & VBR Analytics**: Real-time reverse-engineering of upstream multiplexer strategies, monitoring Null Packet displacement and GOP phase alignment.
*   **Ecosystem Integration**: Providing high-density telemetry to downstream professional transcoders and decoders via bit-exact JSON/Prometheus interfaces.

---

## 1. System Architecture Overview

```text
    +-----------------------+
    |   Network Ingress     | (10G/25G NIC via AF_XDP/DPDK)
    +-----------+-----------+
                |
    +-----------v-----------+
    |   TS SIMD Parser      | (AVX2/AVX-512 Vectorized)
    +-----------+-----------+
                |
    +-----------v-----------+
    |   Timing Engine       | (27MHz Software PLL / Metrology)
    +-----------+-----------+
                |
    +-----------v-----------+
    |   Analytics & Alarms  | (TR 101 290 / RCA / RST)
    +-----------+-----------+
                |
    +-----------v-----------+
    |   Metrics Dispatch    | (JSON / Prometheus / Webhook)
    +-----------------------+
```

---

## 2. Product Line Definition

| Product | Target User | Role |
| :--- | :--- | :--- |
| **TsAnalyzer Engine** | Developers / SI | Extreme Performance Metrology Probe |
| **TsAnalyzer Appliance** | NOC Operators | High-Density Monitoring Platform |
| **Smart Assurance Gateway**| Network Engineers | Inline Signal Repair & Fail-safe Relay |

---

## 3. Positioning & Vision

### 3.1 TsAnalyzer Engine (The Brain)
A bit-exact, deterministic C library and CLI tool. It treats Transport Streams as physical entities, providing nanosecond-level clock reconstruction and strict ETSI TR 101 290 compliance. It is designed to be embedded into larger broadcast systems.

### 3.2 TsAnalyzer Appliance (The Surface)
A multi-channel server architecture that aggregates metrology from multiple Engines. It provides situational awareness through a 7-tier NOC dashboard, long-term SLA tracking, and Root Cause Analysis (RCA) inference.

### 3.3 Smart Assurance Gateway (The Shield)
An inline processing node that monitors signal health in real-time and takes active measures (Pacing, Shaping, or Fail-safe Bypass) to preserve service continuity across unpredictable IP networks.

---

## 4. Core Philosophical Pillars
1.  **Deterministic Measurement**: Identical input must yield bit-identical analytical results.
2.  **Predictive Telemetry**: Use Buffer Safety Margins and Remaining Safe Time (RST) to alert *before* viewer impact.
3.  **Causal Explainability**: Every fault must be attributable to either the Network or the Encoder via quantifiable scoring.

---

## 5. Performance Targets (Scalability Model)

| Total Streams | Average Bitrate | Aggregate Throughput | Target Hardware |
| :--- | :--- | :--- | :--- |
| **128** | 8 Mbps | 1 Gbps | 8 Core / 16GB |
| **256** | 8 Mbps | 2 Gbps | 16 Core / 32GB |
| **512** | 8 Mbps | 4 Gbps | 32 Core / 64GB |
| **1000** | 8 Mbps | 8 Gbps | 64 Core / 128GB |

---

## 6. Industrial Verification Gates
*   **G1 (Throughput)**: 1.0 Gbps - 10.0 Gbps aggregate throughput with zero kernel drops.
*   **G2 (Metrology)**: 100% TR 101 290 P1/P2 coverage; ±10ns PCR jitter precision.
*   **G3 (Determinism)**: MD5-consistent JSON output for identical PCAP input.
*   **G4 (Stability)**: 24h stability with zero memory growth (RSS flat-line).
