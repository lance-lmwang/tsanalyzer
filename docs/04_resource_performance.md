# Resource & Performance Spec: Cloud Edge Density

TsAnalyzer and TsPacer are optimized for high-density **Inline Gateway** operation on standard cloud instances.

## 1. Multi-Tenant Isolation Strategy

For SaaS-grade deployments, TsAnalyzer enforces strict isolation:
* **Memory Sandboxing**: Each stream utilizes a dedicated, pre-allocated pool. Buffer overflows are contained within the tenant's memory segment.
* **Core Pinning**: Worker threads are hard-affinity bound to physical cores, preventing "Noisy Neighbor" jitter from other tenants.
* **Independent Metering**: Prometheus labels (`tenant_id`) ensure per-customer billing and usage tracking.

## 2. 16-Core Node Performance Benchmarks

*Hardware: AWS c6g.4xlarge (16 vCPU, 32GB RAM)*

| Concurrent Streams | Throughput | CPU Usage (Avg) | Mem Usage (RSS) |
| :--- | :--- | :--- | :--- |
| **100** | 1 Gbps | 8% | 5.2 GB |
| **500** | 5 Gbps | 35% | 26.0 GB |
| **1000+** | 10+ Gbps | 72% | 31.5 GB |

* **Linearity**: Throughput scales linearly up to **800 Mbps per physical core**.
* **Latency**: Internal analysis delay remains **< 2us per packet** at P99 peak load.

---

## 3. Cloud Hardening & Self-Healing

### 3.1 Kubernetes Integration
* **HPA (Horizontal Pod Autoscaling)**: Native support for scaling nodes based on aggregate Gbps relay load or active stream count.
* **Auto-Recovery**: Isolated stream segment crashes trigger internal thread restarts in **< 50ms**, ensuring > 99.99% logical uptime.

### 3.2 Fail-Safe Bypass
* **L4 Bypass**: Direct kernel-level socket forwarding is triggered if internal processing latency $\Delta t_{proc}$ hits the **5ms threshold**.
* **Continuity**: This ensures stream continuity even at 100% CPU saturation or analysis engine stall.

### 3.3 Resource Guardrail
* **Saturation Guard**: API rejects new `POST` requests once RSS memory exceeds 80% of node capacity to preserve real-time stability for existing active relays.
