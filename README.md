# TsAnalyzer & TsPacer: OTT Smart Link Assurance Gateway

**TsAnalyzer & TsPacer** form a strictly cloud-first, metrology-driven gateway for MPEG-TS stream integrity and predictive operational assurance. Designed to replace expensive, hardware-bound legacy equipment, this suite provides a quantifiable, predictable, and controllable bridge for OTT encoding, CDN, and edge nodes.

---

## Strategic Value: The Big Nine

1.  **Industrial Metrology**: Full ETSI TR 101 290 (P1/P2/P3) and MDI (DF:MLR) instrumentation.
2.  **Predictive Survivability**: **RST (Remaining Safe Time)** proactive countdown (100ms refresh).
3.  **Smart Forwarding & Relay**: Metrology-driven re-streaming with integrated rate pacing (TsPacer).
4.  **Transparent Failover**: Hardware-grade **L4 Bypass** ensures zero downtime during maintenance or failure.
5.  **ABR-Ready Content Audit**: Deep scanning for **GOP Jitter** and **IDR alignment** consistency.
6.  **Secure Cloud Relay**: Real-time **AES-128/256** decryption monitoring and secure re-wrapping.
7.  **Automated RCA**: Instant domain attribution (Network vs. Encoder) via weighted superposition.
8.  **SLA & Webhooks**: Real-time SLA reporting and event-driven HTTP callbacks for cloud orchestration.
9.  **Automated Forensic Bundle**: capture of raw TS + trace logic triggered by critical risk events.

---

## The Closed-Loop Workflow

Unlike passive side-path tools, we sit "in the wire" to execute an active lifecycle:

1.  **Analyze**: Deep metrology (101 290 / MDI) at 1ms precision.
2.  **Predict**: Calculate RST and RCA score every 100ms.
3.  **Control**: Trigger **TsPacer** for reactive wave-shaping or bitrate adjustment (< 5ms response).
4.  **Verify**: Execute forensic replays and automated CI/CD stress regression.

---

## Deployment Architecture

### Server Mode (High-Density Hub)
A persistent multi-tenant daemon designed for Kubernetes (K8s) orchestration.
*   **SRT Multiplexing**: Single-port Listener mode using `StreamID` routing.
*   **Dynamic API**: REST/gRPC control plane for live task management.
*   **Performance**: 10+ Gbps throughput and 1000+ streams per node.

### CLI Mode (Engineering Tool)
Quick-start diagnostic tool for local file analysis or multicast segment validation.

---

## Build & Quick Start

### Requirements
*   CMake 3.10+, GCC (C11)
*   Linux Kernel (RT-capable recommended)
*   OpenSSL & libsrt (v1.5.4)

### Build
```bash
./build.sh
```

### Run Server Mode (Management API)
```bash
sudo ./build/tsa --server --api-port 8080 --metrics-port 9000
```

### Run CLI Mode (File Analysis)
```bash
./build/tsa -f sample_stream.ts --profile STRICT_SYNC
```

---

## Operational Documentation Suite

*   **[00: Product Overview](docs/00_product_overview.md)** - Vision, Glossary, and Strategy.
*   **[01: Causal Analysis Spec](docs/01_causal_analysis_spec.md)** - RST/RCA mathematical models and action matrix.
*   **[02: Monitoring & PRD](docs/02_monitoring_prd.md)** - Prometheus metrics and Grafana NOC design.
*   **[03: Verification Strategy](docs/03_verification_strategy.md)** - CI/CD stress testing and replay validation.
*   **[04: Resource & Performance](docs/04_resource_performance.md)** - Cloud edge density and hardware hardening.
*   **[05: API Reference](docs/05_api_reference.md)** - C Library and Automation REST endpoints.
*   **[06: Design Review Guide](docs/06_design_review_guide.md)** - Engineering iron rules and implemention guide.
*   **[07: Forensic Guide](docs/07_forensic_verification_guide.md)** - Auditing automated evidence bundles.
*   **[08: Server API Design](docs/08_server_api_design.md)** - Multi-tenant fleet orchestration and webhooks.
*   **[09: Grafana Dashboard Spec](docs/09_grafana_dashboard_spec.md)** - Survival-First NOC visualization layout.
*   **[10: Implementation Roadmap](docs/10_implementation_roadmap.md)** - C11 zero-allocation architecture and CI/CD production gates.
*   **[11: System Architecture Diagram](docs/11_system_architecture_diagram.md)** - Multi-level functional mapping and decision logic.

## License
MIT
