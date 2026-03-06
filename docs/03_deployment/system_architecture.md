# TsAnalyzer: Overall System Architecture

This document defines the end-to-end integration of the **Engine (Probe)**, **NOC Appliance**, and **Assurance Gateway**. It provides a layered blueprint for deploying TsAnalyzer in high-scale broadcast and streaming environments.

---

## 1. System Topology

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

### 2.1 Smart Assurance Gateway (Inline Gateway)
*   **Role**: Situated at the ingress link as an inline node to perform **Pacing**, **Shaping**, and **Fail-safe Bypass**.
*   **Requirements**: Supports dual-NIC transparent forwarding with sub-millisecond latency. Ensures signal compliance by reshaping clumped traffic without introducing drops.
*   **Interfaces**: gRPC/REST for control, PTP for time synchronization.

### 2.2 Engine (Professional Probe)
The Engine is split into a high-speed **Data Plane** and a logic-heavy **Analysis Plane**:
*   **DPDK/AF_XDP Capture**: Kernel-bypass ingestion with NUMA locality and Hugepage support.
*   **SIMD TS Parser**: Vectorized header and PID extraction using AVX2/AVX-512.
*   **Time-Wheel Scheduler**: $O(1)$ repetition checks for PAT/PMT/PCR intervals.
*   **Clock Recovery & T-STD**: Mathematical reconstruction of the sender clock and decoder buffer simulation.
*   **StatMux Inference**: Identifying multiplexer cycles and VBR phase alignment.

### 2.3 NOC Appliance (Aggregation Platform)
*   **Ingest Layer**: Aggregates high-density OTLP/Kafka streams from distributed Probes.
*   **TSDB**: High-cardinality storage for PCR jitter, bitrate, and stateful incidents.
*   **RCA Service**: Cross-stream correlation to identify the root cause of systemic outages.
*   **Dashboard**: The central NOC surface providing Heatmaps, Timelines, and Drill-downs.

### 2.4 Control & Management Plane
*   Centralized orchestration for stream definitions, alarm policies, and Role-Based Access Control (RBAC).
*   Manages firmware updates, license enforcement, and security auditing across the probe fleet.

---

## 3. Data Flow & Interfaces

*   **Ingestion**: Encoders → Gateway → Engine (via NIC HW Timestamps).
*   **Internal Pipeline**: Lock-free SPSC rings facilitate wait-free handoff between Ingestion, Parsing, and Analysis.
*   **Reporting**: Exporters push state snapshots to the Appliance via OTLP or Kafka.
*   **Archiving**: Trigger-based PCAP recording to high-speed NVMe storage with asynchronous migration to S3.

---

## 4. Deployment Standards

### 4.1 Probe Nodes (The Edge)
*   **Hardware**: Dedicated 16–64 core Xeon/EPYC servers with 10G/25G/100G NICs.
*   **Tuning**: Mandatory `isolcpus`, `hugepages`, and NUMA binding.
*   **HA**: Active-active probe pairs for critical trunks to ensure 99.999% monitoring availability.

### 4.2 NOC Platform (The Core)
*   **Infrastructure**: Kubernetes-based deployment for stateless services.
*   **Persistence**: Distributed TSDB and Kafka clusters for high-volume telemetry ingestion.
*   **Security**: Mandatory mTLS for management traffic and network isolation (VLAN/VRF) between Data and Control planes.

---

## 5. Deployment Topologies

1.  **Distributed Edge**: Probes at every encoder/uplink site pushing to a central cloud-based NOC.
2.  **Inline Protection**: Gateway and Probe pairs at ISP handover points for real-time repair and SLA verification.
3.  **Local Hybrid**: Small-scale ( < 200 streams) deployment where Probe and Appliance share the same hardware cluster.
