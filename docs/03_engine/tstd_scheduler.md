# T-STD Hierarchical Scheduler (Professional Grade)

## 1. Overview
The T-STD scheduler implements a **Mutually-Exclusive Hierarchical Decision Tree** with **Opportunistic SI Injection**. It prioritizes physical timing accuracy while achieving maximum ES layer smoothness by utilizing "idle slots" for system information.

## 2. Priority Hierarchy (The Decision Tree)

The `ff_tstd_step_internal` function implements the following priority tiers for every 188-byte physical slot:

| Tier | Level | Action Class | Trigger Condition | Rationale |
| :--- | :--- | :--- | :--- | :--- |
| **P0** | **L0** | **Hard PCR** | `packet_count >= next_pcr_packet` | Physical clock is the non-negotiable master. |
| **P1** | **L1** | **Emergency Preemption** | `es_is_congested == 1` | Prevent TB_n overflow at the expense of temporary PCR jitter. |
| **P2** | **L2** | **SI/PSI Injection**| `psi_consecutive_count < 2` | Bound priority for system metadata (PAT/PMT/SDT). |
| **P3** | **L3** | **Normal ES Payload** | `pick_es_pid()` returns valid PID | Pacing-regulated elementary stream emission. |
| **P4** | **L4** | **NULL Padding** | All above idle | Maintain Constant Bitrate (CBR) physical pipe. |

---

## 3. Opportunistic SI Injection & Preemption Logic
To achieve industrial-grade smoothness, the engine treats SI (PAT/PMT/SDT) as "background noise" that fills the gaps left by the video pacer:
1.  **Pacing Regulation**: The ES Pacer determines if a video packet is legal to emit based on token bucket fullness.
2.  **Slot Utilization**: If the Pacer mandates a wait, the slot is initially marked as `ACT_NULL`.
3.  **SI Interception**: The SI Injector intercepts this `ACT_NULL` and replaces it with a waiting SI packet (L2).
4.  **Token-Regulated Preemption**: If a video stream is lagging beyond 1.1x of the `mux_delay` (slack ratio), it can "preempt" a NULL slot even if the normal pacer is waiting, provided it has accumulated legal tokens.

---

## 4. Delay-Adaptive PI Control (Gain & Corridor)
The pacer dynamically scales its reactivity based on the user-configured `-muxdelay` parameter.

### 4.1 Gain Scaling (Adaptive Responsiveness)
$$Gain = \frac{0.00045}{MuxDelay(s)}$$
*   **Low Latency (e.g., 500ms)**: High gain for rapid recovery from spikes.
*   **High Latency (e.g., 2000ms)**: Low gain for extreme silk-smoothness.

### 4.2 Corridor Adaptive Clamping
The physical emission corridor (max/min bitrate deviation) is also scaled to the buffer depth:
$$Limit = \frac{0.0225}{MuxDelay(s)}$$
$$Range = [1.0 - Limit, 1.0 + Limit]$$
This prevents the engine from attempting corrections that the physical buffer cannot absorb, ensuring VBV compliance.

## 5. Industrial Congestion Defense (L1 Preemption)
In professional environments, a single large I-frame can temporarily "choke" the PCR interval. The **L1 Emergency Preemption** allows the ES payload to "steal" a PCR slot if the ES queue delay exceeds 1000ms and the PCR is within a 2-packet safety window of its deadline. This ensures system stability under extreme peak-to-average bitrate ratios.
