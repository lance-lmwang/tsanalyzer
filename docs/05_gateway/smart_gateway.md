# Smart Assurance Gateway

The Smart Assurance Gateway provides inline signal processing, repair, and fail-safe relay capabilities.

## 1. The Smart Action Matrix

The gateway automatically switches operational states based on the intersection of Remaining Safe Time (RST) and RCA attribution.

| State | RST Threshold | Attribution | Gateway Action |
| :--- | :--- | :--- | :--- |
| **Optimal** | $> 15s$ | `OK` | **Direct Pass-Through**: Lowest latency relay. |
| **Degraded** | $10s - 15s$ | `Any` | **Active Monitoring**: Tag metadata with warnings. |
| **Mitigation**| $5s - 10s$ | `NETWORK` | **Paced Relay**: Engage Bitrate Smoother. |
| **Critical** | $< 5s$ | `Any` | **Forensic Capture**: Dumps 500ms of TS evidence. |

---

## 2. Pacing & Shaping Engine (Bitrate Smoother)

Uses high-precision timing to eliminate network-induced jitter and fix upstream multiplexing non-compliance.

### 2.1 T-STD Compliant Reshaping (Grooming)
Unlike traditional muxers (like FFmpeg) that may clump audio packets, TsAnalyzer Pro performs **Metrology-Guided Reshaping**:
*   **Audio De-clumping**: The gateway identifies clustered audio packets that violate the T-STD buffer model. It utilizes a **Grooming Buffer** to spread these packets evenly across the video PID gaps based on the recovered PCR timeline.
*   **Micro-Interleaving**: Every single TS packet's departure time is calculated relative to the target CBR rate. If no service data is scheduled for a specific time-slot, a Null Packet (0x1FFF) is inserted with nanosecond precision.
*   **Result**: The egress stream passes strict TR 101 290 Priority 3 buffer auditing on laboratory-grade hardware (e.g., Tektronix MTS4000).

### 2.2 Mechanism
*   **High-Precision Pacing**: Employs `clock_nanosleep(TIMER_ABSTIME)` to pace UDP batches exactly at the PCR-recovered bitrate.
*   **Buffer Management**: Maintains a shallow (~100ms) lock-free buffer to absorb ingress micro-bursts while maintaining a constant output leak rate.

---

## 3. Fail-Safe: Watchdog Bypass

Because the gateway operates inline, it must never become a point of failure.

### 3.1 Bypass Thresholds
If processing latency $\Delta t_{proc} > 5ms$ for a contiguous window of 100 packets, the gateway panic-switches to **Transparent L4 Bypass**.

### 3.2 Implementation (v3 Roadmap)
*   **Current**: Userland copy-bypass (Fast thread-to-thread pointer transfer).
*   **Planned (AF_XDP)**: Direct hardware-level redirection using **XDP_REDIRECT**. This allows the network card to forward traffic at the driver level if the analysis process hangs, achieving near-zero latency even during failure.
