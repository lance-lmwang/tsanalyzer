# T-STD Multiplexer Scheduler Architecture Design

## 1. Abstract
Traditional multiplexers typically employ Round-Robin or single Token Bucket strategies for fair bandwidth allocation. This design introduces a **3-Tier Hybrid EDF (Earliest Deadline First)** scheduling model integrated with an **Autonomous Clock-Driven PCR Injection** mechanism. This architecture redefines the multiplexing process as a deterministic constraint satisfaction problem, ensuring zero decoder underflow and strict T-STD physical buffer compliance.

## 2. Core Invariants
The scheduler must strictly satisfy the following hard constraints:

* **Constraint A: Zero Underflow (Decoder Safety)**
    $$emit\_time(AU_i) \le DTS_i$$
* **Constraint B: T-STD Compliance (Overflow Prevention)**
    $$TB\_fullness \le TB\_size$$
* **Constraint C: PCR Determinism (Clock Fidelity)**
    PCR injection must be decoupled from the ES scheduling loop to ensure zero-jitter clock recovery.

## 3. The 3-Tier Queueing Model
The scheduler evaluates PIDs at each transmission slot and categorizes them into priority domains:

### 🔴 L0: PANIC QUEUE
* **Trigger:** $delay\_ms > 2 \times mux\_delay\_ms$ OR $deadline \le v\_stc$.
* **Policy:** Highest priority, hard preemption of all other traffic. Must satisfy Constraint B.

### 🟡 L1: URGENT QUEUE
* **Trigger:** $deadline - v\_stc < margin$ ($50ms$ for Audio, $10ms$ for Video).
* **Policy:** EDF within the domain. Allows bypassing the Token Gate but strictly enforces Constraint B.

### 🟢 L2: NORMAL QUEUE
* **Trigger:** Standard operating range.
* **Policy:** Token-gated selection using Hybrid EDF, with feedback control to stabilize Video FIFO around a target latency ($800ms$).

## 4. Autonomous Clock-Driven PCR Injection
PCR is no longer a downstream effect of ES scheduling. It is established as the **System Heartbeat**:

* **Determinism**: PCR injection intervals are defined by packet counts rather than time calculations to eliminate floating-point jitter.
    $$pcr\_interval\_packets = \frac{muxrate \times PCR\_Interval\_ms}{Packet\_Size\_Bits}$$
* **Hard Preemption**: The PCR injector pre-empts the scheduler loop every $N$ packets. When `stc_ticks >= next_pcr_stc`, the scheduler MUST inject a PCR packet, even if it mid-stream of a larger ES buffer transmission.

## 5. Queue Feedback Control
To prevent FIFO divergence without threshold-jumping, we employ a continuous control loop for video streams:

```c
// Feedback Control Logic
if (pid->cls == TSTD_CLASS_VIDEO) {
    int64_t error = delay_ms - TARGET_LATENCY_MS;
    if (error > 0) {
        score -= error * SCALE_FACTOR; // Aggressive drain bias
    }
}
```
This forces the scheduler to continuously favor video streams that accumulate buffer pressure, stabilizing the queue around the `TARGET_LATENCY`.

## 6. Mathematical Schedulability & Proof
**Theorem:** *Under bounded workload conditions and enforced hard TB limits (Constraint B), the combination of L0/L1 preemptive rescue and L2 feedback control guarantees satisfaction of all timing and buffer constraints.*

1.  **Stability**: L0 PANIC creates a negative backlog drift, preventing divergence.
2.  **Safety**: Hard TB guards ensure the scheduler never forces a physical buffer overflow.
3.  **Jitter Minimization**: Autonomous PCR injection ensures the clock anchor is independent of the jitter-prone ES scheduling logic.

## 7. Compliance with TR 101 290
The scheduler maintains a maximum PTS/PCR discrepancy of **750ms**. PIDs exceeding this limit are elevated to PANIC status, forcing an immediate buffer flush and service rate adjustment.
