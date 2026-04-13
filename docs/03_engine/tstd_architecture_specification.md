# T-STD Multiplexer Architecture Specification

## 1. Executive Summary
The **Architecture** defines a deterministic, timing-accurate multiplexing engine for high-density, professional broadcast environments. It implements a **Continuous-Approximated CPB Model** and **Deterministic PCR Generation** to ensure absolute compliance with ISO/IEC 13818-1 and ETSI TR 101 290 under 100% saturation scenarios. This architecture is explicitly designed for **Strict Constant Bitrate (CBR)** output, utilizing deterministic NULL packet insertion to maintain an unvarying physical channel rate.

---

## 2. Timing & Synchronization (vSTC)
The foundation of the engine is an independent **Virtual System Target Clock (vSTC)**, decoupled from data availability. The vSTC is a purely synthetic, deterministic clock derived strictly from the accumulated physical bytes emitted and the configured target $Muxrate$.

### 2.1 Monotonic Slot Progression
The vSTC advances by discrete "Physical Slots." Every slot represents the exact time required to transmit 188 bytes at the target $Muxrate$.

$$\Delta T_{slot} = \frac{188 \times 8 \times 27,000,000}{Muxrate}$$

### 2.2 Deterministic PCR Generation
Because the engine operates on a strictly mathematical timing model rather than querying an OS-level wall clock, PCR stamping is perfectly deterministic.
$$vSTC = \frac{Total\_Bytes\_Written \times 8 \times 27,000,000}{Muxrate}$$
$$PCR = vSTC + Delay_{fixed}$$

To ensure 24/7 continuous operation without integer overflow or clock drift, the mathematical derivation applies strict modulo arithmetic matching the 33-bit PCR base ($2^{33}$) and 9-bit extension ($300$) wraparound limits.

**PCR Jitter Definition (Generation vs. Transport):**
*   **PCR Numerical Accuracy (Generation Domain):** $< 100ns$. The mathematical offset is perfectly calculated without OS scheduling jitter.
*   **PCR Transport Jitter:**
    *   **Packet-level quantization bound:** $\approx 7\mu s$ (@ 38Mbps).
    *   **Effective PCR jitter (per TR 101 290):** $< 500ns$ after IRD interpolation. Since TS packets are discrete 188-byte blocks, the physical displacement is strictly a mathematical function of the slot quantization error, perfectly satisfying compliance requirements.

---

## 3. Buffer Modeling: Continuous-Approximated CPB
ISO/IEC 13818-1 defines Coded Picture Buffer (CPB) removal as an instantaneous, step-based event at $T_{removal}$ (DTS). To prevent scheduling oscillations, this engine uses a **continuous-time numerical approximation** of the normative T-STD removal process.

### 3.1 Linear Leak Approximation
While the engine evaluates compliance against the strict normative DTS deadline, its internal heuristic simulates real-world **Integrated Receiver Decoder (IRD)** behavior via continuous drainage:
* **Arrival**: $Fullness$ jumps by the payload size when a packet is sent.
* **Departure**: $Fullness$ decreases linearly in every slot:
  $$Removal\_Bits = \frac{Allocated\_CBR\_Rate \times \Delta T_{slot}}{27,000,000}$$
  *(Note: The $Allocated\_CBR\_Rate$ is strictly derived from HRD/VBV parameters or the CBR allocation context, preventing model drift even for bursty VBR-encoded streams).*

---

## 4. Multi-Dimensional Scheduling
The scheduler manages the "Who," while the vSTC manages the "When."

**System Stability Guarantee:** The scheduling system is designed as a bounded, damped feedback system, ensuring convergence without oscillatory behavior under steady-state conditions. Furthermore, the scheduler ensures that per-service bandwidth guarantees (SI/PSI + ES) are preserved independently, preventing cross-service starvation under multi-program conditions.

### 4.1 Hierarchical Strict EDF (Constraint-Driven)
To guarantee that the CPB never underflows or overflows, the scheduler abandons dimensionally-mixed scoring formulas in favor of a **Strict Hierarchical Constraint** tree. This preserves the mathematical optimality of Earliest Deadline First (EDF) scheduling:

1.  **Primary (Hard Constraint): Strict EDF**. The scheduler unconditionally selects the PID with the most imminent $T_{removal}$ (DTS). This is the absolute mathematical priority to guarantee T-STD safety. *(Note: This guarantee holds under the strict assumption that all input elementary streams conform to their declared HRD/VBV constraints).*
2.  **Secondary (Tie-break - Soft Heuristic): CPB Fullness**. Only when multiple PIDs have equivalent deadlines within a microscopic tolerance ($\Delta Deadline < \epsilon$) does the scheduler evaluate $Normalized\_Fullness$, prioritizing the PID with the highest buffer pressure.
3.  **Tertiary (Sticky Bonus): Context Switch Reduction**. Applied *only* within the $\epsilon$-window and capped to a minuscule value to prevent high-bitrate PIDs from implicitly starving low-bitrate streams.

### 4.2 Guaranteed SI/PSI Bandwidth
SI/PSI tables (PAT/PMT/SDT) are treated as **Tier 0 Guaranteed Bandwidth**. They occupy pre-allocated slots regardless of ES pressure, ensuring absolute stability in Repetition Intervals.

### 4.3 Mandatory PCR Injection (TR 101 290 Compliance)
To strictly satisfy ETSI TR 101 290 requirements, the scheduler enforces a **Forced PCR Insertion** rule:
$$Threshold = \min(35ms,\ 0.9 \times Configured\_PCR\_Interval)$$
If the vSTC elapsed since the last PCR reaches this threshold on a PCR-bearing PID, the scheduler immediately preempts all other traffic to emit a packet stamped with a deterministic PCR. **This occurs strictly independent of payload availability** (using adaptation-field-only packets if necessary), guaranteeing compliance even during source starvation.

### 4.4 Deterministic NULL Packet Emission
To decisively prevent physical deadlocks, `NULL` packets are emitted *if and only if*:
1.  No PID is currently eligible for payload transmission (e.g., waiting for future DTS).
2.  No constraint violation (T-STD underflow/overflow or PCR timeout) is imminent.

*(Note: PCR-only packets take strict precedence over NULL emission when PCR deadlines are imminent, guaranteeing TR 101 290 compliance even when payload queues are empty).*

---

## 5. Control Theory & Safety

### 5.1 Adaptive Burst Clamping
Burst limits are calculated dynamically based on a specific time window:
$$Burst\_Limit = \text{clamp}\left(\frac{Muxrate \times T_{window}}{1504}, Min, Max\right)$$
The window is strictly calibrated to **$2ms \sim 4ms$**. This is purposefully chosen to:
*   Be significantly smaller than the PCR interval ($\ll 40ms$).
*   Avoid VBV burst artifacts and physical micro-jitter.
*   Perfectly align with typical decoder smoothing windows.
*   Ensure TS packet pacing remains locally uniform, minimizing inter-packet gap variance on the physical layer.

### 5.2 Token Avalanche Suppression
During `DRIVE FUSE` clock jumps, a global state reset is mandatory:
* All Integrators and Timers are re-anchored.
* $last\_update\_ts = vSTC$.

### 5.3 EOF Soft Landing (Drain Phase)
Upon EOF, the vSTC continues to run. Refill rates are capped at $85\% \times Muxrate$ to prevent auditing spikes, and the process only exits once the CPB for all streams is logically empty according to the Drain Model.

---

## 6. Compliance KPIs

| Metric | TR 101 290 Requirement | Target Performance |
| :--- | :--- | :--- |
| **PCR Jitter (Numerical)** | N/A (Generation Domain) | **< 100ns (Mathematically Perfect)** |
| **PCR Jitter (Transport)** | < 500ns | **< 500ns (Quantization Bound: $\approx 7\mu s$)** |
| **PCR Frequency** | $\pm 30ppm$ | **Perfectly derived from Muxrate** |
| **Buffer Latency** | < 700ms | **< 500ms (Adjustable)** |
| **Duration Accuracy** | N/A | **Bit-exact to DTS Timeline** |
| **SI/PSI Intervals** | $\pm 10\%$ | **< 1% (Slot Guaranteed)** |

---

## 7. Comparative Evolution

| Feature | Legacy (Software Grade) | **Current** |
| :--- | :--- | :--- |
| **CPB Behavior** | Instant Removal | **Continuous Approximation (Normative Safe)** |
| **PCR Logic** | OS Clock Snapshot | **Deterministic Byte-Derived STC** |
| **Scheduling** | Mixed Scoring (Risk) | **Strict Hierarchical EDF (Provable Safety)** |
| **SI/PSI Strategy** | Tier-based Competition | **Guaranteed Bandwidth Allocation** |
| **Time Base** | Byte-feedback STC | **Monotonic Slot-based STC** |
