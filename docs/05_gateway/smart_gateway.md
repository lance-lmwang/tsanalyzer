# Smart Assurance Gateway

The Smart Assurance Gateway provides inline signal processing, repair, and fail-safe relay capabilities.

---

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

## 3. Multi-Input Resilient Failover

The gateway supports **Active-Standby** and **Threshold-Based Failover** to ensure 99.999% delivery uptime.

### 3.1 Input Source Ranking
Users can define multiple inputs (e.g., Primary SRT, Backup UDP).
*   **Heartbeat Monitoring**: The Metrology Engine continuously monitors all inputs, even those in standby.
*   **Switching Triggers**: A switch is initiated if the active source hits:
    *   `Signal_Health_Level == INVALID`
    *   `RST < 2.0s` (Predictive switching)
    *   `CC_Error_Rate > Threshold`

### 3.2 Seamless Handoff
To prevent downstream decoder sync-loss during a switch:
*   **PTS Alignment**: The gateway utilizes the **Clock Recovery Model** to align the PTS/DTS of the backup source with the outgoing timeline before committing the switch.
*   **CC Continuity**: The `transport_continuity_counter` is restamped at the handoff point to maintain a perfect monotonic sequence.

---

## 4. Selective Stream Redirection (Demux-Routing)

The gateway can perform dynamic PID-level routing and selective metrology.
*   **Service Extraction**: Routing specific programs from an MPTS to different destinations.
*   **Active PID Management**: Dynamically subscribing to (`join_pid`) or dropping (`leave_pid`) components based on real-time analysis needs or external API requests.

---

## 5. Fail-Safe: Watchdog Bypass

Because the gateway operates inline, it must never become a point of failure.

### 5.1 Bypass Thresholds
If processing latency $\Delta t_{proc} > 5ms$ for a contiguous window of 100 packets, the gateway panic-switches to **Transparent L4 Bypass**.

### 5.2 Implementation (v3 Roadmap)
*   **Current**: Userland copy-bypass (Fast thread-to-thread pointer transfer).
*   **Planned (AF_XDP)**: Direct hardware-level redirection using **XDP_REDIRECT**. This allows the network card to forward traffic at the driver level if the analysis process hangs, achieving near-zero latency even during failure.
