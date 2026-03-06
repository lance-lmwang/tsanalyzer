# TsAnalyzer: Overall System Architecture

This document defines the high-level integration of the **Smart Assurance Gateway**, **Engine (Probe)**, and **NOC Appliance**. It serves as the master blueprint for deploying TsAnalyzer Pro in industrial-scale broadcast and high-fidelity streaming environments.

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
*   **Requirements**: Dual-NIC transparent forwarding with sub-millisecond latency. Reshapes clumped traffic to ensure T-STD compliance without introducing packet drops.
*   **Interfaces**: gRPC/REST for control, PTP for time synchronization.

### 2.2 Engine (The Professional Probe)
The Engine is split into an $O(1)$ **Data Plane** and a logic-heavy **Analysis Plane**:
*   **Ingest Stage**: DPDK/AF_XDP kernel-bypass with NUMA locality and Hugepage support.
*   **Parser Stage**: SIMD vectorized header extraction using AVX2/AVX-512.
*   **Metrology Stage**: Clock recovery via high-precision PLL, T-STD buffer simulation, and StatMux inference.
*   **Analytics Stage**: Localized event correlation and TR 101 290 state machine updates.

---

## 3. Deployment Standards (Performance & HA)

To maintain deterministic measurement at scale, Probe nodes must be hardened to instrument-grade standards.

### 3.1 Probe Node Hardening
*   **CPU**: 16–64 cores (AMD EPYC / Intel Xeon) with full AVX-512 support.
*   **Mandatory Kernel Tuning**:
    *   `isolcpus`: Isolate analysis cores from the general OS scheduler.
    *   `nohz_full`: Enable full tickless mode on isolated cores.
    *   `rcu_nocbs`: Offload RCU callbacks to management cores.
    *   `intel_idle.max_cstate=0`: Disable deep sleep states to eliminate wake-up latency.
*   **Memory**: High-speed ECC Registered RAM; 1GB Hugepages MUST be enabled for the ring buffers.

### 3.2 High Availability (HA)
*   **Active-Active Sampling**: Critical trunks are monitored by dual probes simultaneously for cross-check validation.
*   **Kubernetes Core**: The NOC platform (Appliance) is deployed as a resilient microservice stack with TSDB replication and Kafka clusters.

---

## 4. Security & Observability Redlines

### 4.1 Security Architecture
*   **Plane Isolation**: Data Plane and Control Plane traffic MUST be physically or logically isolated via VLAN or VRF.
*   **Encrypted Signaling**: All inter-node gRPC and REST communication must use mTLS with certificate rotation.
*   **Forensic Protection**: PCAP forensic data must be encrypted at rest (AES-256) with strict RBAC access logs.

### 4.2 Critical Monitoring Metrics
*   **Packet Integrity**: Kernel-level drops, HW-TS accuracy, MDI-DF.
*   **Metrology Fidelity**: PCR Jitter (Mean/Std/Max), Clock Drift (ppm), Buffer Safety Margin (BSM %).
*   **Engine Health**: NIC Queue depth, SPSC ring occupancy, Core temperature variance.

---

## 5. Deployment Topologies

1.  **Distributed Edge**: Probes deployed near encoders/uplinks, pushing high-density telemetry to a central cloud Appliance.
2.  **Inline Protection**: Gateway and Probe pairs at ISP handover points for real-time signal repair and SLA verification.
3.  **Local Hybrid**: Converged deployment where Probe and Appliance share the same hardware cluster (for deployments < 200 streams).
