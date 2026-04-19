# T-STD Hierarchical Scheduler

## 1. Overview
The T-STD scheduler implements a **Mutually-Exclusive Hierarchical Decision Tree** with **Opportunistic SI Injection**. It prioritizes physical timing accuracy while achieving maximum ES layer smoothness by utilizing "idle slots" for system information.

## 2. Priority Hierarchy (The Decision Tree)

| Level | Action Class | Trigger Condition | Impact on ES Smoothness |
| :--- | :--- | :--- | :--- |
| **L0** | **Hard PCR** | `packet_count >= next_pcr_packet` | Micro-jitter (Unavoidable) |
| **L1** | **ES Payload** | `pick_es_pid()` returns valid PID | **Primary Source of Smoothness** |
| **L2** | **Opportunistic SI**| `action == NULL` AND SI data ready | **Zero Impact (Hole-filling)** |
| **L3** | **NULL Padding** | All above idle | Constant Bitrate baseline |

---

## 3. Core Logic: Opportunistic SI Injection
To achieve industrial-grade smoothness, the engine treats SI (PAT/PMT/SDT) as "background noise" that fills the gaps left by the video pacer:
1.  The Scheduler first consults the PI-driven Pacer to determine if a video packet is legal to emit.
2.  If the Pacer mandates a wait (to maintain smoothness or refill VBV), the slot is initially marked as `ACT_NULL`.
3.  The SI Injector intercepts this `ACT_NULL` and replaces it with a waiting SI packet.
4.  **Result**: The physical video packet interval remains absolutely uniform, eliminating the "sawtooth" jitter pattern common in traditional multiplexers.

---

## 4. Delay-Adaptive PI Control
The pacer dynamically scales its reactivity based on the user-configured `-muxdelay` parameter, ensuring optimal performance across different latency profiles.

### 4.1 Gain Scaling
$$Gain = \frac{0.00045}{MuxDelay(s)}$$
*   **Low Latency**: High gain for rapid recovery.
*   **High Latency**: Low gain for extreme silk-smoothness.

### 4.2 Corridor Adaptive Clamping
The physical emission corridor is also scaled to the buffer depth:
$$Limit = \frac{0.0225}{MuxDelay(s)}$$
$$Range = [1.0 - Limit, 1.0 + Limit]$$
This prevents the engine from attempting corrections that the physical buffer cannot absorb.
