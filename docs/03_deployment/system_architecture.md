# TsAnalyzer System Architecture

This document describes the complete system architecture of **TsAnalyzer**, including the three primary product components:

*   **TsAnalyzer Engine**: High-performance TS analysis core.
*   **TsAnalyzer Appliance**: NOC monitoring and analytics platform.
*   **Smart Assurance Gateway**: Inline protection and signal repair node.

The architecture separates **data plane processing** from **control plane orchestration** to achieve high throughput and scalability.

---

## 1. High-Level System Architecture

```mermaid
flowchart LR

subgraph Broadcast_Network
    A[Encoder / StatMux]
    B[IP Network]
    C[TS Streams]
end

subgraph Gateway
    G1[Smart Assurance Gateway]
end

subgraph Engine
    E1[Packet Capture]
    E2[SIMD TS Parser]
    E3[Metric Engine]
    E4[PCR Clock Model]
    E5[T-STD Buffer Simulator]
    E6[TR 101 290 Detector]
end

subgraph Appliance
    A1[Metric Bus]
    A2[Stream Aggregator]
    A3[Root Cause Engine]
    A4[Time-Series Database]
    A5[NOC Dashboard]
end

A --> B
B --> C
C --> G1
G1 --> E1

E1 --> E2
E2 --> E3
E3 --> E4
E4 --> E5
E5 --> E6

E6 --> A1
A1 --> A2
A2 --> A3
A3 --> A4
A4 --> A5
```

---

## 2. System Layer Breakdown

### 2.1 Smart Assurance Gateway (Edge Protection Layer)
The **Smart Assurance Gateway** sits inline in the network path and performs real-time monitoring and mitigation.
*   **Responsibilities**: TS health monitoring, packet pacing and shaping, fail-safe bypass, and early anomaly detection.
*   **Placement**: `Encoder → Gateway → Core Network → Decoder`.
*   **Value**: Ensures signal continuity under unstable IP conditions by reshaping clumped traffic.

---

## 3. TsAnalyzer Engine (Measurement Core)

The **Engine** performs ultra-high-speed transport stream analysis at the "Edge of Silicon."

### 3.1 Design Goals
*   **Multi-Gbps Throughput**: Sustaining 10Gbps+ aggregate analysis.
*   **Lock-free Processing**: 8M pps pipeline with zero mutex contention.
*   **Deterministic Analysis**: Bit-identical results regardless of system load.

### 3.2 Key Modules
*   **Packet Capture**: High-speed acquisition using DPDK, AF_XDP, or PF_RING.
*   **SIMD TS Parser**: Vectorized (AVX2/AVX-512) parsing of 188-byte packets, enabling tens of millions of packets per second.
*   **PCR Clock Model**: Reconstructs encoder clocks with ±10ns precision for jitter and drift metrology.
*   **T-STD Buffer Simulator**: Implements normative decoder behavior to predict underflow/overflow risk.
*   **TR 101 290 Engine**: Event-driven $O(1)$ detection across thousands of concurrent streams.

---

## 4. TsAnalyzer Appliance (Observability Platform)

The **Appliance** aggregates metrics from multiple Engines into a unified operational view.

*   **Metric Bus**: High-throughput telemetry pipeline (Kafka, NATS, or OTLP).
*   **Stream Aggregator**: Groups raw metrics by service, program, and multiplex.
*   **Root Cause Engine**: Correlates indicators (e.g., `Loss + Jitter → Network Fault`) to identify failure domains.
*   **Time-Series Database**: Long-term storage (Prometheus, VictoriaMetrics, or ClickHouse) for SLA auditing.
*   **NOC Dashboard**: Implements the **7-Layer Observability Model** for intuitive situational awareness.

---

## 5. Control Plane & Orchestration

The control plane manages the fleet of probes and gateways.
*   **Management**: REST/gRPC API for stream registration and configuration.
*   **Registry**: Centralized directory of all active monitoring tasks.
*   **Orchestration**: `Operator → API → Appliance → Engine`.

---

## 6. Scalability & Data Flow Summary

### 6.1 Scaling Model
A single deployment scales horizontally by adding Engine nodes to an Appliance cluster, supporting **1000+ concurrent streams**.

### 6.2 Data Flow Logic
`Transport Stream → Gateway Protection → Engine Measurement → Metric Aggregation → Analytics & RCA → NOC Visualization`

---

## 7. Core Design Principles

1.  **Determinism**: Identical inputs produce identical analytical hashes.
2.  **High Throughput**: Systems designed for 10Gbps+ line-rate metrology.
3.  **Operational Visibility**: Technical metrics translated into human-actionable NOC insights.
