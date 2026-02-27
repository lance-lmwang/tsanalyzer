# Design Review & Engineering Implementation Guide

This document defines the mandatory engineering standards, mathematical foundations, and system-level requirements for the **OTT Smart Link Assurance Gateway**. All code changes must be audited against these criteria.

---

## 1. Inline Stability & Fail-Safe

### 1.1 Watchdog & Bypass (Critical)
The gateway MUST NOT become a single point of failure (SPOF).
- **Watchdog**: Every packet processing cycle is timed.
- **Bypass Condition**: If processing latency $\Delta t_{proc} > 5ms$ for 100 consecutive packets, the engine MUST trigger **Transparent L4 Bypass**.
- **Recovery**: Transition back to Analysis mode only after 10s of sustained stability.

---

## 2. Mathematical Foundations (Metrology)

### 2.1 Precision Timing & Clock Policy
- **128-bit Logic**: All time deltas MUST use `int128_t` nanoseconds.
- **Clock Source**:
    - **Analysis**: `CLOCK_MONOTONIC` for PCR/PTS arrival.
    - **Pacing**: `CLOCK_MONOTONIC_RAW` to avoid NTP-induced step corrections during rate shaping.
- **Allan Deviation**: Used for long-term clock stability ($1s, 10s, 60s$). Must use $O(1)$ updates.

### 2.2 Bitrate Engine (Instrument-Grade)
PID bitrates MUST use the **Proportional Weighting** method to ensure consistency across MPTS:
$$\text{PID Bitrate} = \frac{\Delta \text{PID Packets}}{\Delta \text{Total Packets}} \times \text{Total Muxrate (from PCR)}$$

### 2.3 Pacing & CBR Uniformity
To maintain professional CBR behavior during smart forwarding:
- **Null Packet Usage**: PID 0x1FFF only.
- **Uniformity Index**: Calculated as $1 - CV$, where $CV = \frac{\sigma_{gap}}{\mu_{gap}}$ for 100 consecutive null packets. Target $\approx 1.0$.
- **Adjustment Limit**: TsPacer adjustment MUST be capped at $\pm 50\%$ of nominal bitrate.

---

## 3. Memory & Concurrency Contract (C11)

### 3.1 Wait-Free SeqLock
A **Sequence Lock** MUST be used for atomic metric snapshots.
- **Writer**: `memory_order_release`. Increment (odd), copy, increment (even).
- **Reader**: `memory_order_acquire`. Optimistic read with retry if sequence is odd or `begin != end`.

### 3.2 Cache-Line & Zero-Allocation
- **Alignment**: Shared structures MUST be padded to `alignas(64)`.
- **Static Memory**: All resources MUST be pre-allocated. `malloc` is forbidden in the data path.

---

## 4. Observability & SLA

### 4.1 Delta-Quality Metrics
Dashboards MUST visualize:
- **Jitter Neutralization**: Ingress MDI-DF vs. Egress stability.
- **SLA Integrity**: `tsa_forward_success_pct` and `tsa_forward_latency_us` per destination.
- **ARQ Tax**: Ratio of SRT retransmissions to total packets.
