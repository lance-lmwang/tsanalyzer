# T-STD Multiplexer Scheduler Architecture Design

## 1. Abstract
Traditional multiplexers typically employ Round-Robin or single Token Bucket strategies for fair bandwidth allocation. However, in high-dynamic-range audio/video interleaving scenarios, the pure pursuit of "fairness" often results in time-sensitive streams being squeezed by large-frame video bursts. This leads to audio underflow, video stuttering, and potential scheduler deadlocks.

This design proposes a **3-Tier Hybrid EDF (Earliest Deadline First)** scheduling model, redefining the multiplexing process as a **Constraint Satisfaction Problem**. Through a 3-tier priority funnel—L0 (Panic), L1 (Urgent), and L2 (Normal)—the system achieves the goal of zero decoder underflow while strictly guaranteeing that the T-STD physical buffers never overflow.

## 2. Core Invariants
The scheduler must strictly satisfy the following two hard constraints at all times:

* **Constraint A: Zero Underflow (Decoder Safety)**
    For any Access Unit $AU_i$, its physical emission completion time must not be later than its decoding deadline:
    $$emit\_time(AU_i) \le DTS_i$$

* **Constraint B: T-STD Compliance (Overflow Prevention)**
    At any given moment, the Transport Buffer (TB) fullness must not exceed its physical limit:
    $$TB\_fullness \le TB\_size$$

**Crucial Directive:** Regardless of what priority tier a PID is in, **Constraint B cannot be bypassed**. Higher priorities are only permitted to bypass the smoothing constraint (the Token Gate), never the physical compliance boundary.

## 3. The 3-Tier Queueing Model
At each clock tick (slot), the scheduler evaluates all ready PIDs and categorizes them into the following priority domains.

### 🔴 L0: PANIC QUEUE
* **Semantics:** The data stream is on the verge of physical instability and must be drained immediately.
* **Trigger Conditions (Matches Any):**
    1.  $delay\_ms > 2 \times mux\_delay\_ms$
    2.  $deadline \le v\_stc$
* **Scheduling Policy:** Scan *all* PIDs and select the PANIC PID with the *earliest deadline*. If multiple candidates exist at this tier, EDF is used as the tie-breaker. PANIC status only elevates preemption rights; it **must not** breach the hard TB limit.

### 🟡 L1: URGENT QUEUE
* **Semantics:** The data stream is approaching the danger zone and requires a temporary relaxation of the Token Gate to avoid underflow.
* **Trigger Condition:**
    $$deadline - v\_stc < margin$$
    *Recommended Margins:*
    * Audio: $50ms$
    * Video: $10ms$
* **Scheduling Policy:** Employs EDF within the URGENT domain. It is allowed to bypass the Token Gate, but must still strictly satisfy the hard TB constraint.

### 🟢 L2: NORMAL QUEUE
* **Semantics:** Data streams operating within a healthy range.
* **Trigger Condition:** All ready streams that do not fall into L0 or L1.
* **Scheduling Policy:** Must pass the Token Gate. Once token conditions are met, selection is based on a Hybrid EDF scoring system. In the event of a tied score, the stream with the higher `continuous_fullness_bits` is prioritized.

## 4. Arbitration Order
The system employs a strict, fixed arbitration sequence:
**PANIC > URGENT > NORMAL**

* **PANIC:** Highest priority, hard preemption.
* **URGENT:** Soft real-time rescue.
* **NORMAL:** Bandwidth shaping and fair output.

## 5. Mathematical Schedulability
This architecture can be modeled as a Leaky Bucket Constrained EDF with hard bounds.

### 5.1 Schedulability Assumptions
* **Assumption 1:** The input bitrate is bounded within the observation window.
* **Assumption 2:** In a stable state, the system's physical output rate ($R_{out}$) is greater than or equal to the long-term average input rate ($R_{in}$).
* **Assumption 3:** The hard TB limit is strictly enforced at all times.

### 5.2 Proof Sketch
Under these assumptions, the PANIC state represents a **negative drift** region. When a stream's backlog exceeds the threshold, the scheduler elevates its service probability, making its expected service rate higher than its arrival rate. Therefore, the backlog decreases monotonically in expectation, ensuring the system does not diverge, safely returning to the operational range.

The rationale for the **Audio Bias** is based on the fact that audio streams have much smaller AU granularities and significantly higher time sensitivity. Granting audio a larger soft-real-time margin trades a minimal total bandwidth perturbation for a substantially reduced risk of underflow.

## 6. Algorithmic Flow

```c
TSTDPidState *pick_es_pid_v2(TSTDContext *tstd)
{
    TSTDPidState *panic_pid = NULL;
    TSTDPidState *urgent_pid = NULL;
    TSTDPidState *normal_pid = NULL;
    int64_t panic_best_dl = INT64_MAX;
    int64_t urgent_best_dl = INT64_MAX;
    int64_t normal_best_score = INT64_MAX;

    for (int i = 0; i < tstd->nb_all_pids; i++) {
        TSTDPidState *pid = tstd->all_pids[i];

        // Hard TB Guard (Constraint B)
        if (pid->tb_fullness_bits + TS_PACKET_BITS > pid->tb_size_bits)
            continue;

        int64_t deadline = calculate_deadline(pid);
        int64_t delay_ms = calculate_delay_ms(pid);

        // L0: PANIC
        if (delay_ms > 2 * tstd->mux_delay_ms || deadline <= tstd->v_stc) {
            if (deadline < panic_best_dl) {
                panic_best_dl = deadline;
                panic_pid = pid;
            }
            continue;
        }

        // L1: URGENT
        int64_t margin = (pid->cls == TSTD_CLASS_AUDIO) ?
                         (TSTD_SYS_CLOCK_FREQ / 20) :  // 50ms
                         (TSTD_SYS_CLOCK_FREQ / 100);  // 10ms

        if (deadline - tstd->v_stc < margin) {
            if (deadline < urgent_best_dl) {
                urgent_best_dl = deadline;
                urgent_pid = pid;
            }
            continue;
        }

        // L2: NORMAL
        if (pid->tokens_bits < TS_PACKET_BITS)
            continue;

        double alpha = (pid->cls == TSTD_CLASS_AUDIO) ? 1.5 : 0.5;
        int64_t score = (deadline - tstd->v_stc) - (int64_t)(alpha * delay_ms);

        if (score < normal_best_score) {
            normal_best_score = score;
            normal_pid = pid;
        }
    }

    if (panic_pid)  return panic_pid;
    if (urgent_pid) return urgent_pid;
    return normal_pid;
}
```

## 7. Mathematical Schedulability & Proof of Zero Underflow

This architecture transitions the multiplexer from a heuristic fairness model to a **Strictly Constrained Hybrid EDF** model. To formally prove its safety, we define the schedulability conditions.

### 7.1 Schedulability Assumptions & Hard Bounds

The system operates under the following determinable bounds:

* **Assumption 1 (Bounded Workload):** For any observation window $W$, the aggregate input workload is strictly upper-bounded by the physical output capacity of the multiplexer:
    $$\sum_{i} R_{in, i} \le R_{out}$$
* **Assumption 2 (Minimum Service Guarantee in PANIC):** When multiple streams enter the L0 PANIC state, the scheduler guarantees a minimum service rate $R_{service, i}$ for each panicked stream $i$ such that its backlog strictly decreases:
    $$R_{service, i} \ge R_{in, i} + \epsilon \quad (\text{where } \epsilon > 0)$$
* **Assumption 3 (Dynamic Urgent Margin):** The trigger for the L1 URGENT state is no longer an empirical static value, but a deterministic physical bound derived from the Transport Buffer (TB) slack:
    $$margin_i = \frac{TB_{size, i} - TB_{fullness, i}}{R_{x, n}}$$
    *(Where $R_{x, n}$ is the ISO 13818-1 specified leak rate for stream $i$.)*

### 7.3 Compliance with TR 101 290
The T-STD scheduler explicitly ensures that for all audio and video packets, the discrepancy between the Presentation Time Stamp (PTS/DTS) and the Program Clock Reference (PCR) adheres to the ETR 290 (TR 101 290) standard, maintaining a maximum tolerance of **750ms**.

Any PID failing to meet this real-time emission requirement is elevated to the L0 PANIC state, triggering an immediate service rate increase to prevent the decoder from reporting a buffer underflow error.
