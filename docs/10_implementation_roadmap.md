# TsAnalyzer Pro: Engineering Implementation & Testing Roadmap

This roadmap defines the sequential delivery of the **OTT Smart Link Assurance Gateway**, strictly aligned with the [System Architecture Diagram](./11_system_architecture_diagram.md).

---

## Phase 1: Gateway Core & Fail-Safe Infrastructure
**Core Goal**: Establish the high-performance "Inline" data plane and the dual-path logic (Bypass vs. Analysis).

### 1.1 Memory & Concurrency Contract
- Implement `alignas(64)` padded structures to eliminate False Sharing.
- Implement fully pre-allocated memory pools ensuring **zero `malloc` calls** during real-time relay.
- Implement Wait-Free SeqLock for atomic telemetry snapshots.

### 1.2 Watchdog & L4 Bypass (Fail-Safe)
- Implement the **Fail-Safe Watchdog** micro-thread to monitor packet processing latency ($\Delta t_{proc}$).
- Implement the **Transparent L4 Bypass** path (direct socket passthrough).
- **Deliverable**: A functional "Smart Relay" that defaults to **Direct Pass-Through** and flips to **Bypass** if analysis overhead exceeds 5ms.

### 1.3 Precision Timing Engine
- Integrate `int128_t` nanosecond logic for all arrival/pacing timestamps.
- Implement $O(1)$ Allan Deviation for long-term clock stability.

---

## Phase 2: Metrology & RCA Mathematical Brain
**Core Goal**: Deliver the "Brain" capable of deep protocol auditing and fault prediction.

### 2.1 Metrology Matrix
- Implement TR 101 290 (P1/P2/P3) and **MDI (DF:MLR)** state machines.
- Implement ES-layer auditing (GOP Jitter, AV-Sync drift, HDR metadata).

### 2.2 Predictive RCA/RST Engine
- Implement **RST (Remaining Safe Time)** models for both Network and Encoder domains.
- Implement the **Weighted Superposition RCA** logic to attribute faults based on normalized causal factors ($C_i$).

---

## Phase 3: Action Engine & Active Repair (TsPacer)
**Core Goal**: Deliver the "Action Engine" to close the loop between prediction and control.

### 3.1 Smart Action Matrix
- Implement the **Action Engine** to coordinate between three states:
    - **Healthy**: Forward + Audit.
    - **Impaired**: Engage **TsPacer** for repair.
    - **Critical**: Trigger **Forensic Capture**.

### 3.2 TsPacer Repair Module
- Implement proportional weighting bitrate estimation.
- Implement jitter neutralization via `0x1FFF` null-packet pacing ($\pm 50\%$ adjustment range).
- **Verification**: Assert Egress MDI-DF is reduced by $> 50\%$ compared to Ingress.

---

## Phase 4: SaaS Orchestration & Egress Security
**Core Goal**: Deliver the multi-tenant control plane and secure egress relay.

### 4.1 High-Density SaaS Daemon (`tsa-server`)
- Implement REST/gRPC API with JWT authentication.
- Implement O(1) hash-table routing for 1000+ streams and **Tenant Isolation**.

### 4.2 Secure Egress & Events
- **Orchestrate libsrt AES-128/256**: Integrate transport-layer decryption (Ingest) and re-encryption (Egress) using socket options.
- Implement **Cloud Webhook** dispatcher for real-time RCA/RST event notification.
- Finalize **Forensic Bundle** (.zip) generation with `manifest.json`.

---

## CI/CD Production Gates (Final)
1. **Contract Audit**: eBPF-based "Zero Malloc" and "Zero Lock" verification in data threads.
2. **Predictive Accuracy**: Predicted RST vs. Actual underflow error $\le \pm 1s$.
3. **Attribution Precision**: RCA primary fault domain accuracy $\ge 98\%$.
4. **Relay Integrity**: Zero packet loss during Watchdog $\rightarrow$ Bypass transitions.
5. **Gateway Efficiency**: Aggregate node throughput $\ge 10 Gbps$ with $< 100\mu s$ relay latency.
