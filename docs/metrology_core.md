# TsAnalyzer: Metrology Core & Physics Specification

This document defines the rigorous mathematical and temporal framework of TsAnalyzer. It treats the analyzer as a **Software-Defined Measurement Instrument**, where every result is derived from modeled physics and is experimentally reproducible.

---

## 1. The Timing Model: Deterministic Temporal Foundation

TsAnalyzer establishes a temporal framework independent of operating system time and wall-clock behavior. All measurements are derived from three strictly separated timing domains.

### 1.1 Temporal Domains
1.  **Hardware Arrival Time (HAT)**: Monotonic nanoseconds assigned by the NIC at DMA completion. Defines packet causality.
2.  **System Time Clock (STC)**: Reconstructed 27 MHz timeline derived from MPEG PCR values. Defines the encoder's intent.
3.  **Virtual STC (VSTC)**: The primary analysis axis where STC is mapped onto the Hardware Arrival axis, ensuring bit-identical results across live and replay sessions.

### 1.2 STC Reconstruction & Interpolation
Between PCR samples ($PCR_1$ at $HAT_1$ and $PCR_2$ at $HAT_2$):
*   **Interpolation**: Intermediate values derive from the linear slope: $STC_{rate} = (PCR_2 - PCR_1) / (HAT_2 - HAT_1)$.
*   **Precision**: Uses **__int128 fixed-point arithmetic** to ensure overflow protection and architecture-independent results.
*   **Overall Jitter (PCR_OJ)**: Calculated by comparing actual arrival with the expected linear progression:
    $$Jitter = (PCR_{curr} - PCR_{ref}) - \frac{(HAT_{curr} - HAT_{ref}) \times 27,000,000}{1,000,000,000}$$

### 1.3 High-Precision Pacing (Pacer Side)
The `tsp` pacer uses a **Hybrid Scheduler**:
*   Wait > 2ms: `clock_nanosleep(TIMER_ABSTIME)`.
*   Wait < 2ms: `busy-wait + pause`.
*   Capped token bucket (10ms) to suppress micro-bursts and preserve NIC buffer integrity.

---

## 2. ETSI TR 101 290 Compliance Standard

TsAnalyzer implements the industry-standard metrology for service availability and quality.

### 2.1 Priority 1 (P1): Service Availability
| Metric | ID | Threshold | Implementation |
| :--- | :--- | :--- | :--- |
| **TS_sync_loss** | P 1.1 | 5-In / 2-Out | Strict FSM: 5 packets to lock, 2 to lose. |
| **PAT_error** | P 1.3 | 500ms | PID 0 interval > 13.5M V-STC units. |
| **Continuity_error**| P 1.4 | Immediate | CC sequence check per PID, respecting Discontinuity Indicator. |
| **PMT_error** | P 1.5 | 500ms | Referenced PMT interval > 13.5M V-STC units. |

### 2.2 Priority 2 (P2): Quality & Timing
| Metric | ID | Threshold | Implementation |
| :--- | :--- | :--- | :--- |
| **Transport_error** | P 2.1 | Immediate | `transport_error_indicator` bit audit. |
| **CRC_error** | P 2.2 | Immediate | CRC32 mismatch for PAT, PMT, CAT, NIT, SDT, EIT. |
| **PCR_repetition** | P 2.3 | 40ms | Interval check + heartbeat watchdog. |
| **PCR_accuracy** | P 2.4 | ± 500ns | Real-time metrology via 27MHz Software PLL. |
| **PTS_error** | P 2.5 | 700ms | PTS interval > 18.9M V-STC units (90kHz). |

---

## 3. Advanced Metrology & Clock Analytics

### 3.1 3D PCR Topology (ISO/IEC 13818-1 Annex I)
PCR jitter is mathematically decomposed into 3 orthogonal vectors:
1.  **PCR_AC (Accuracy)**: Transmitter clock precision error.
2.  **PCR_DR (Drift Rate)**: Long-term Linear Regression (Trend) of PCR vs. Physical System Clock to detect crystal aging/deviation.
3.  **PCR_OJ (Overall Jitter)**: Phase jitter introduced by network transit.

### 3.2 Piecewise Constant Bitrate (PCBR) Model
For offline forensics, the engine uses previous interval bitrates to predict future PCRs, providing a baseline immune to local scheduling jitter.

---

## 4. ISO/IEC 13818-1 Annex D: T-STD Buffer Model

Reconstructs normative decoder behavior as a deterministic physical system (Leaky Bucket).

### 4.1 Buffer Evolution
Metrology is driven by **Access Units (AU)** reassembled from PES:
$$Buffer(t_2) = Buffer(t_1) - (DrainRate \times \Delta VSTC) + ArrivingBits$$
*   **AU Arrival**: `buffer += size`. Trigger **Overflow** if $B_n > capacity$.
*   **Decoder Removal**: Scheduled at $DTS$. Trigger **Underflow** if starvation occurs.

### 4.2 Remaining Safe Time (RST)
The primary predictive metric:
$$RST = \frac{Buffer_{fullness}}{DrainRate}$$
Indicates time remaining (in VSTC units) before decoder starvation, enabling proactive alerts before visual corruption occurs.

---

## 5. Network Physics: MDI & IAT Profiling

### 5.1 Media Delivery Index (RFC 4445)
*   **Delay Factor (DF)**: Network-induced decoder stress, bound strictly to **Hardware RX Timestamps** to bypass OS latency.
*   **MLR (Media Loss Rate)**: Count of missing packets over the measurement window.

### 5.2 IAT (Inter-Arrival Time) Histograms
Real-time statistical profiling of packet spacing (buckets: <1ms, 1-2ms, 2-5ms, 5-10ms, >10ms). Detects switch-level micro-bursts that "average" bitrate stats would hide.

---

## 6. Execution & Runtime Constraints

### 6.1 Synchronized Sampling Barrier
To prevent bitrate inversion, all counters and clocks are "frozen" behind a global barrier at the start of a metrology cycle. Calculations are performed strictly on deltas between freeze-frames.

### 6.2 Atomic Evaluation Order
To ensure platform independence, events at the same timestamp follow this priority:
1. Drain Evolution → 2. Decoder Removal (DTS) → 3. AU Arrival.

### 6.3 128-bit Fixed-Point Guard
All critical timing and buffer math uses **__int128 fixed-point (Q64.64)**. Floating-point math is forbidden in the analysis path to guarantee bit-identical results across x86 and ARM architectures.

### 6.4 Precision Summation (Kahan Compensated)
To prevent precision loss in long-term drift estimation (24h+ runs), the engine employs **Kahan Compensated Summation** for floating-point accumulators used in non-critical reporting paths.
