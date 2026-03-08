# Clock Domains & Synchronization Model

TsAnalyzer maintains a strict multi-domain clock model to isolate network-induced artifacts from encoder-side impairments. This document defines the hierarchy and interaction of these domains.

## 1. Clock Domain Hierarchy

To achieve high-integrity metrology, the system maintains four independent reference timelines:

| Domain | Frequency | Source | Primary Purpose |
| :--- | :--- | :--- | :--- |
| **SystemClock (Wall)** | 1.0 GHz (ns) | `CLOCK_MONOTONIC` | **Physical Metrology**: Throughput calculation, timeout watchdogs, and wall-clock drift. |
| **IngressClock (Rx)** | 1.0 GHz (ns) | NIC/Kernel | **Jitter Analysis**: Provides the precise arrival moment ($t_{rx}$) for each packet. |
| **PCRClock (Logic)** | 27.0 MHz (ticks) | Program PCR | **Business Metrology**: Piecewise bitrate estimation, PCR Jitter ($OJ/AC$), and STC recovery. |
| **MediaClock (PES)** | 90.0 KHz (units) | PTS / DTS | **QoE Analysis**: Lip-sync audit, A/V skew, and PTS repetition monitoring. |

---

## 2. Synchronization & Isolation Principles

### 2.1 Domain Isolation (Strict Tiering)
Metrology results MUST NOT mix units from different domains without explicit conversion via the recovered clock slope.
*   **Physical Tier**: bits are divided by SystemClock $\Delta t$.
*   **Business Tier**: bits are divided by PCRClock $\Delta ticks$.
*   **Mixing domains** (e.g., dividing bits by logic ticks to get a wall-clock rate) is the primary source of scaling errors and MUST be avoided.

### 2.2 God's Eye View (Internal Self-Sampling)
To ensure thread-safe and deterministic calculation, every metrology tier implements **Internal Self-Sampling**:
*   Calculation functions (e.g., `tsa_calc_stream_bitrate`) sample their own reference time (e.g., `CLOCK_MONOTONIC`) at the instant of calculation.
*   This prevents "time-drift" between the observer (the calculation thread) and the subject (the packet counter), ensuring accurate results even under high CPU load.

### 2.3 STC Recovery (PCR-to-Wall Mapping)
The system uses a high-order Software PLL to map the discrete **PCRClock** to the continuous **SystemClock**:
$$STC(t) = Slope \times t + Intercept$$
*   **Slope**: Represents the frequency drift (PPM) of the encoder's crystal relative to the analyzer's clock.
*   **Intercept**: Represents the initial phase offset.

---

## 3. Metrology Anchoring

### 3.1 Anchor Resets
On sequence breaks (Discontinuity Indicator) or clock wrap-arounds (Looping), the analyzer performs an **Anchor Reset**:
1.  The baseline PCR value is reset.
2.  The private packet counter for that clock domain is synchronized to the current packet sequence.
3.  The calculation window is discarded to prevent "Ghost Bitrates" caused by time jumps.

### 3.2 Snapshot Synchronization
Snapshots represent a "frozen" view of all domains. To maintain consistency:
*   Wall-clock time ($n$) is captured at the snapshot start.
*   Logical STC ($stc$) is calculated based on the $n$ using the current regression slope.
*   All subsequent PID-level metrics are attributed to this synchronized window.
