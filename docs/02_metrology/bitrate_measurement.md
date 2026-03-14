# Bitrate Measurement & Metrology Standards (v2.0)

This document defines the high-precision bitrate measurement architecture of TsAnalyzer, designed to align with Tektronix MTS and industrial carrier-grade standards.

## 1. The Bitrate Matrix (Multi-Dimensional Statistics)

Unlike basic tools that report a single "bitrate", professional metrology requires a multi-dimensional view to assess both bandwidth usage and signal stability. TsAnalyzer implements a 4-tier matrix for both the overall Transport Stream and individual PIDs.

| Metric | Time Window | Engineering Purpose |
| :--- | :--- | :--- |
| **Current** | 500ms (Settled) | Real-time link occupancy. Refined by EMA smoothing. |
| **Average (Avg)** | Session Lifetime | Total Payload Weight. Calculated as `TotalBits / TotalTime`. |
| **Peak (Maximum)** | Session Lifetime | Buffer Stress Analysis. Detects instantaneous bursts that may cause overflow. |
| **Minimum (Min)** | Session Lifetime | Link Starvation Analysis. Detects gaps in delivery. |

---

## 2. Physical Bitrate (Total TS Bitrate)

The Physical Bitrate represents the raw throughput of the Transport Stream (Layer 2).

### 2.1 Definition & Formula
The engine counts every valid 188-byte TS packet (including Video, Audio, PSI, and Null packets).
*   **Formula**: `Bitrate_bps = (ΔPackets * 1504) / ΔWallClock_ns`
*   **Constraint**: Minimum settlement window is **500ms** to平滑 OS 调度抖动。

### 2.2 Packet Integrity (Anti-Deduplication)
In accordance with ISO/IEC 13818-1, TsAnalyzer treats the stream as a physical reality.
*   **No Deduplication**: Even if packets have duplicate PID+CC, they are counted. Duplicates occupy real bandwidth and may carry critical timing info.
*   **Null Packets**: PID 0x1FFF is fully counted in the physical rate but excluded from business (payload) rates.

---

## 3. Business Bitrate (PCR Payload Bitrate)

The Business Bitrate represents the constant bitrate (CBR) of a specific program derived from its own clock reference.

### 3.1 Mathematical Derivation
Instead of wall-clock time, it uses the 27MHz PCR clock:
`Bitrate_bps = (ΔPackets * 1504 * 27,000,000) / ΔPCR_ticks`

### 3.2 MPTS Multi-Program Isolation
TsAnalyzer isolates each program into an independent **Clock Domain**. PCR values from Program A never interfere with Program B. The global `pcr_bitrate_bps` is the **algebraic sum** of all unique program bitrates.

---

## 4. Operational Logic: Replay vs Live

### 4.1 Replay Mode (Deterministic Speed)
When analyzing files, the engine processes data as fast as possible.
*   **Virtual Clock**: Bitrate is calculated based on simulated arrival timestamps (Nominal 10Mbps interval or provided by Pacer).
*   **Final Settlement**: A forced "Flush" is executed at the end of the file to ensure the final report includes the last window of data.

### 4.2 Active Bitrate Decay
To mimic the "Persistence" effect of professional hardware analyzers:
*   If a PID disappears, its reported bitrate is **decayed by 50% per window** rather than instantly dropping to zero. This allows operators to catch "flickering" streams.

---

## 5. Implementation Guardrails

1.  **No Type Mixing**: Never use PCR Ticks and System Nanoseconds in the same formula.
2.  **Atomic Counting**: All packet counters used for metrology are updated via atomic operations.
3.  **Hysteresis**: Peak and Min values are only updated after a window has fully settled to avoid reporting single-packet noise.
