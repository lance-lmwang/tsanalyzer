# Hardware Compatibility List (HCL): Professional Metrology

To achieve nanosecond-precision PCR jitter analysis and 10Gbps line-rate throughput, TsAnalyzer requires specific hardware support, particularly for **Hardware Timestamping (SO_TIMESTAMPING)**.

---

## 1. Verified Network Interface Cards (NICs)

| Vendor | Series | Model Examples | Features | Status |
| :--- | :--- | :--- | :--- | :--- |
| **Intel** | **700 Series** | X710-DA2, XL710 | HW TS, SR-IOV | **Verified (Gold)** |
| **Intel** | **500 Series** | X520-DA2, X540 | HW TS (Legacy) | **Verified (Silver)** |
| **Mellanox**| **ConnectX-4+** | CX4121A, CX5, CX6 | HW TS, DPDK | **Verified (Gold)** |
| **Broadcom** | **NetXtreme-E** | BCM57414 | HW TS | **Verified** |

### 🛑 Note on Virtual NICs
Standard `virtio-net` and `vmxnet3` do **not** support hardware timestamping. For VM deployments, use **PCI Passthrough** or **SR-IOV Virtual Functions** to provide the engine with direct access to the physical NIC.

---

## 2. CPU Recommendations

TsAnalyzer's SIMD-vectorized parser performs best on CPUs with the following instruction sets:

1.  **AVX-512**: Recommended for high-density 10Gbps+ appliances (Xeon Scalable 2nd Gen+, EPYC 7003+).
2.  **AVX2**: Standard for 1Gbps - 5Gbps multi-channel analysis.
3.  **SSE4.2**: Minimum requirement for hardware acceleration.

---

## 3. BIOS & OS Optimization

*   **C-States**: Disable deep sleep states (C3/C6) to reduce wakeup latency for the 27MHz Software PLL.
*   **Turbo Boost**: Recommended to be set to a fixed frequency or "Performance" profile to ensure deterministic timing math.
*   **Kernel**: Linux Kernel 5.4+ is required for optimal `recvmmsg` and `SO_TIMESTAMPING` performance.
