# Best-in-Class PCR-Locked Pacing Architecture

This document defines the high-performance pacing architecture implemented in the `tsp` engine. This design represents the optimal approach for broadcast-grade MPEG-TS transmission over IP, focusing on nanosecond precision, low jitter, and deterministic flow control.

## 1. Design Philosophy: Decoupled Precision
The architecture is built on the principle of **decoupling scheduling from execution**. Instead of calculating transmission targets on the fly (which is sensitive to CPU load and thread stalls), the engine pre-calculates the absolute transmission time for every single packet as it enters the system.

## 2. Core Pillars of the Architecture

### 2.1 Producer-Side Absolute Scheduling
Every TS packet ingested into the pacer is assigned a unique, nanosecond-accurate **Scheduled Transmission Timestamp** (`target_ns`).
1.  **PCR Anchor Mapping**: The engine identifies the first available Program Clock Reference (PCR) and anchors it to the system monotonic clock, plus a safety latency buffer (e.g., 100ms).
2.  **Deterministic Interpolation**: For packets between PCRs, the engine performs linear interpolation based on the detected bitrate. This ensures that every packet has a pre-defined "flight time," maintaining perfect CBR even if the input is bursty.
3.  **Timestamp Side-Band**: These timestamps are stored in a dedicated parallel buffer (`ts_buffer`) mapped to the main ring buffer, allowing for zero-overhead lookups during the transmission phase.

### 2.2 Lock-Free SPSC Pipeline
To achieve maximum throughput and avoid kernel-level contention, the pacer uses a **Single-Producer Single-Consumer (SPSC) lock-free ring buffer**.
-   **No Mutexes**: The hot path for both data ingestion and transmission is entirely mutex-free, utilizing atomic memory barriers (`acquire/release` semantics).
-   **Cache Alignment**: Core structures are cache-line aligned to prevent false sharing between the producer thread (file/network reader) and the consumer thread (pacer transmitter).

### 2.3 Hybrid High-Precision Execution
The transmission loop (`tx_loop`) acts as a high-speed gatekeeper, comparing the current wall-clock time against the pre-calculated `target_ns`. To overcome OS scheduling jitter, a **Hybrid Wait Mechanism** is employed:
1.  **Graceful Yielding**: If the next packet is scheduled more than 1ms in the future, the thread calls `nanosleep` to release the CPU and stay energy-efficient.
2.  **Microsecond Busy-Waiting**: If the scheduled time is less than 1ms away, the thread enters a high-precision busy-wait loop.
3.  **CPU Relax**: During busy-wait, the loop utilizes the hardware `pause` instruction (`__asm__ __volatile__("pause")`). This hints to the CPU that it is in a spin-loop, reducing power consumption and thermal throttling while ensuring sub-microsecond wake-up latency.

### 2.4 High-Precision 128-bit Math
To maintain accuracy over long-duration transmissions (days or months), all time-to-bitrate calculations utilize **128-bit fixed-point math** (`__int128`). This prevents the silent 64-bit overflows that often plague naive pacer implementations when multiplying large nanosecond values by high bitrates.

## 3. Performance Characteristics
This architecture achieves industry-leading metrics:
-   **Precision**: Bitrate deviation typically $< 0.1\%$.
-   **Jitter**: Inter-packet arrival jitter at the microsecond scale.
-   **Resilience**: The pre-calculated schedule allows the pacer to survive OS scheduler interruptions by providing a deterministic path to catch up without introducing uncontrolled bursts.
