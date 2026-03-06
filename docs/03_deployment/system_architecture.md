# TsAnalyzer: Overall System Architecture

This document defines the high-level integration of the **Smart Assurance Gateway**, **Engine (Probe)**, and **NOC Appliance**. It provides a layered blueprint for deploying TsAnalyzer in industrial-scale broadcast and high-fidelity streaming environments.

---

## 1. Global System Topology

```mermaid
flowchart LR
  subgraph External
    encoders[Encoders / Upstream Mux]
    cdn[CDN / WAN]
    monitoring_src[Telemetry Sources]
  end

  subgraph Inline_Gateway[Smart Assurance Gateway]
    gw_nic[NIC (inline)]
    gw_local_proc[Inline Processing
    (Pacing / Shaping / Bypass)]
    gw_ctrl[Gateway Control Plane
    (gRPC/REST)]
  end

  subgraph Data_Plane[Engine (Probe) - Data Plane]
    dpdk[DPDK / AF_XDP Capture]
    simd[SIMD TS Parser]
    demux[Stream Demux & PID Routing]
    timewheel[Time-Wheel Scheduler]
    clockrec[Clock Recovery & PCR Modeling]
    tstd[T-STD Buffer Simulation]
    statmux[StatMux Inference]
    metrics_rb[Lock-free Ring Buffers]
  end

  subgraph Analysis_Plane[Engine - Analysis]
    policy[Rule Engine (TR101290, RCA)]
    exporter[Metrics Exporter
    (Prometheus / OTLP / Kafka)]
    pcap_store[PCAP / Archive]
  end

  subgraph Appliance[NOC Appliance]
    ingest[Metric Ingest (Kafka/OTLP)]
    tsdb[Time Series DB
    (Prometheus/TSDB)]
    rca_srv[RCA / Correlation Service]
    dashboard[NOC Dashboard & Alerts]
    alerting[Alerting Bus (PagerDuty/Slack/Email)]
    storage[Long-term storage (S3)]
  end

  subgraph Control_Plane[Management & Orchestration]
    config[Config API
    (gRPC/REST)]
    auth[Auth & RBAC]
    ui[Admin UI]
  end

  encoders -->|IP / UDP| gw_nic
  gw_nic --> gw_local_proc -->|pass-through or shaped| dpdk

  dpdk --> simd --> demux --> metrics_rb --> clockrec
  metrics_rb --> tstd
  metrics_rb --> statmux
  clockrec --> policy
  tstd --> policy
  statmux --> policy
  policy --> exporter
  exporter --> ingest
  ingest --> tsdb
  tsdb --> dashboard
  rca_srv --> dashboard
  exporter --> pcap_store
  pcap_store --> storage

  config --> control_plane[Control Plane]
  control_plane --> exporter
  auth --> ui
  dashboard --> alerting

  monitoring_src --> ingest
  cdn --> dpdk
```

---

## 2. Key Component Definitions

### 2.1 Smart Assurance Gateway (The Shield)
*   **Role**: An inline node performing real-time **Pacing**, **Shaping**, and **Fail-safe Bypass**.
*   **Requirements**: Dual-NIC transparent forwarding with sub-millisecond latency. Reshapes clumped traffic to ensure T-STD compliance without introducing drops.
*   **Interfaces**: gRPC/REST for control, PTP for time synchronization.

### 2.2 Engine (The Professional Probe)
The Engine is split into an $O(1)$ **Data Plane** and a logic-heavy **Analysis Plane**:
*   **Ingest**: DPDK/AF_XDP kernel-bypass with NUMA locality and Hugepage support.
*   **Parser**: SIMD vectorized header extraction using AVX2/AVX-512.
*   **Metrology**: Clock recovery via high-precision PLL and T-STD buffer simulation.
*   **RCA Engine**: Localized event correlation and fault classification.

### 2.3 NOC Appliance (The Intelligence Platform)
*   **Ingest Layer**: Asynchronous aggregation of high-density OTLP/Kafka telemetry from distributed Probes.
*   **Persistence**: TSDB for metrics and S3 for long-term PCAP forensic storage.
*   **Operational Surface**: The central dashboard providing Heatmaps, Timelines, and Root Cause Inference.

---

## 3. Deployment Standards (Performance & HA)

### 3.1 Probe Scaling Model
To maintain deterministic measurement at scale, Probe nodes must be hardened:
*   **CPU**: 16–64 cores (AMD EPYC / Intel Xeon) with AVX-512.
*   **Tuning**: Mandatory `isolcpus`, `nohz_full`, `rcu_nocbs`, and `intel_idle.max_cstate=0`.
*   **NIC**: 10/25/40/100GbE with hardware timestamping support.

### 3.2 High Availability (HA)
*   **Active-Active Probes**: Critical trunks are sampled by dual probes simultaneously for cross-check validation.
*   **Kubernetes Appliance**: The NOC platform is deployed as a resilient microservice stack with TSDB replication and Kafka clusters.

---

## 4. Security & Observability

### 4.1 Security Redlines
*   **Management Isolation**: Data Plane and Control Plane traffic MUST be isolated via VLAN or VRF.
*   **Encrypted Signaling**: All gRPC and REST communication must use mTLS.
*   **Data Protection**: PCAP forensic data must be encrypted at rest (SSE) with strict RBAC access.

### 4.2 Key Monitoring Metrics
*   **Transport**: Packet drops, HW-TS accuracy, MDI-DF.
*   **Metrology**: PCR Jitter (Mean/Std/Max), Clock Drift (ppm), Buffer Safety Margin.
*   **System**: NIC Queue depth, SPSC ring occupancy, Core temperature.

---

## 5. Deployment Topologies

1.  **Distributed Edge**: Probes deployed near encoders/uplinks, pushing telemetry to a central cloud-based Appliance.
2.  **Inline Protection**: Gateway and Probe pairs at handover points for real-time signal repair and SLA verification.
3.  **Local Hybrid**: Converged deployment where Probe and Appliance share the same hardware cluster (typically for small-scale < 200 streams).
