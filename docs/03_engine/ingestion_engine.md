# Ingestion Engine: Physical Capture

Layer 1 of the engine is responsible for getting packets into memory with minimal jitter and maximum throughput.

## 1. Hardware-Assisted Timing (HAT)

The ingestion layer leverages NIC-level timestamping to ensure that network micro-bursts are captured accurately.
*   **Mechanism**: Uses `SO_TIMESTAMPING` to retrieve the nanosecond RX time directly from the NIC driver.
*   **Significance**: Bypasses the non-deterministic delay introduced by Linux kernel IRQ handling and process scheduling.

## 2. Zero-Copy Ingress Path

To maintain 1Gbps throughput, data is moved using a pointer-passing architecture.
*   **recvmmsg**: Packets are received in batches of 64 or 128 to reduce the number of syscalls.
*   **SIMD Sync Search**: Uses the **Hardware Abstraction Layer (HAL)** to execute vectorized searches for the `0x47` sync byte.
    *   **AVX2**: Scans 32 bytes in a single cycle.
    *   **SSE4.2**: Optimized 16-byte scans for legacy cloud environments.
*   **SPSC Ring Buffer**: A single-producer, single-consumer lock-free ring buffer connects the I/O thread to the Metrology Worker.
*   **Alignment**: All buffer entries are cache-line aligned (`alignas(64)`) to prevent false sharing between CPU cores.

## 3. CPU Core Pinning

The ingestion threads are hard-pinned to specific cores on the same NUMA node as the NIC to minimize memory access latency.
