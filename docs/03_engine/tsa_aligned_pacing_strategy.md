# TSA-Aligned Pacing Strategy & Physical Layer Rate Shaping

## 1. Overview and Motivation

This document defines the production-grade architecture for a broadcast-grade multiplexer pacing controller. The design mark a transition from reactive feedback to **Conservation-Based Rate Shaping (C-BRS)**. The ultimate goal is to achieve near-perfect physical determinism (the "Absolute Straight Line") while ensuring 100% compliance with ISO 13818-1 and TR 101 290 under real-world OS scheduling jitter.

## 2. System Architecture (The Four Planes)

The controller is structured into four decoupled logical planes to isolate metrology from execution:

*   **Observation Plane:** Simulates a T-STD decoder model with a **60% Golden Ratio** target occupancy. It provides the ground truth for buffer-driven conservation.
*   **Control Plane (The Brain):** High-frequency decision engine implementing the Tiered Equilibrium Model and Energy-Balanced loops.
*   **Safety Plane (The Guard-rail):** Enforces a multi-tier temporal staircase to prevent buffer underflow/overflow (P1.4/P1.5 violations).
*   **Shaping Plane (The Execution):** Implements Credit-Based Scheduling to transform logical rate commands into precise Inter-Packet Gaps (IPG).

## 3. Mathematical Controller Core

The target output rate is governed by a multi-variable closed-loop system:

### 3.1 Energy-Balanced Physical Anchor (Laminar Anchor)
The physical pipe diameter ($R_{base}$) is anchored to long-term energy conservation rather than short-term input tracking:
$$R_{base}(t) = R_{nominal} + \gamma \cdot [ (B_{avg} - B_{target}) - \alpha \cdot (R_{out\_avg} - R_{nominal}) ]$$
*   **$\gamma$ (Glacier Factor):** High time-constant (30s-60s) to absorb systemic encoder drift.
*   **Anchor Cap:** $R_{base}$ is strictly constrained within $\pm 1.5\%$ of $R_{mux}$ to protect the master PCR timeline.

### 3.2 Continuous Gain Envelope (Asymmetric PI)
To eliminate "control hunting" and "breathing," the Proportional Gain $K_p$ is a continuous, asymmetric function of the error $e = \text{delay\_ratio} - 0.60$:
$$K(e) = \begin{cases} K_{high}(e), & e < 0 \text{ (Underflow Risk)} \\ K_{low}(e), & e > 0 \text{ (Overflow Risk)} \end{cases}$$
Small errors result in near-zero gain (Atomic Lock), while large errors trigger smooth, progressive correction.

### 3.3 Physical Derivative Damping (Slope Suppression)
A weak damping term is applied to the output rate changes to eliminate sub-0.05Hz oscillations:
$$\text{Damping} = K_d \cdot (R_{out\_avg} - R_{out\_prev})$$

## 4. Physical Layer Shaping & Scheduling

### 4.1 Credit-Based Flow Control (Asynchronous Pacing)
Traditional `usleep()` based pacing is replaced by a **Credit Accumulator** to bypass OS scheduling jitter:
1.  **Accumulate:** $\text{Credit} += R_{out} \cdot dt$.
2.  **Burst-Protect:** Capped credit balance to prevent flood after OS freeze.
3.  **Emit:** Atomic packet emission driven by credit balance.

### 4.2 Dynamic Time-Anchored Bucket (Soft-Ramp)
Bucket depth is dynamically scaled and temporal-locked to the stream's intrinsic clock:
$$T_{bucket} = \text{clamp}(Linear\_Ramp(\text{Bitrate}), \text{PCR\_Period} \times 1.5, 80\text{ms})$$

## 5. Latency and Priority Isolation (Audio Protection)

### 5.1 EDF Scheduling with Jitter Penalty
Audio/Metadata packets are scheduled using an Earliest-Deadline-First (EDF) priority to ensure timing compliance.

### 5.2 Audio Budget Constraint
To prevent audio bursts from destabilizing the laminar video flow, a window-based cap is enforced (e.g., max 15% of total slots).

## 6. Cold-Start Protection (Soft-Start)

During the first 20 seconds of execution, a tiered jitter envelope is applied to prevent massive Pace spikes while the VBV buffer is filling from zero, ensuring a "Flat-line Startup" characteristic.

## 7. KPIs and Verification Metrology

| Metric | Target | Detection Method |
| :--- | :--- | :--- |
| **Bitrate Delta** | **$\le 88\text{kbps}$** | Aligned PCR-Window Audit. |
| **PCR Jitter (AC)** | **$\le 150\text{ns}$** | 2-point IAT Continuous Audit. |
| **Spectral Leakage** | **No peaks > 0.03Hz** | PSD Analysis. |
| **Energy Error** | **$\le 0.1\%$ / 60s** | Cumulative Bitstream Integrity Tally. |

---
**Status: APPROVED FOR PRODUCTION**
This specification represents the global peak of software-defined multiplexer pacing logic.
