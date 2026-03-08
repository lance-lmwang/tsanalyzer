# Industrial Metrology Alignment: libltntstools Methodology

This document defines how TsAnalyzer aligns its metrology engine with the architectural principles found in industry-standard tools like `libltntstools`.

## 1. Overview
To achieve carrier-grade accuracy, TsAnalyzer utilizes **Anchor-Based Piecewise Measurement**. This method ensures that bitrate calculation is tied to the hardware-derived PCR timeline rather than unstable software timers.

## 2. Core Algorithmic Principles

### 2.1 Piecewise Measurement Model
Instead of using a sliding wall-clock window, TsAnalyzer adopts the **Piecewise Model**:
*   **Measurement Interval**: Bitrate is calculated strictly for the duration between two consecutive PCR samples of the same PID.
*   **Anchor Synchronization**: When a PCR arrives, the engine records the current global packet count ($P_{total}$) as a "Metrology Anchor."
*   **The Delta Formula**:
    $$Bitrate_{bps} = \frac{(P_{total} - P_{anchor}) \times 1504 \times 27,000,000}{PCR_{now} - PCR_{last}}$$

### 2.2 Multiplex-Aware Occupancy
Aligned with professional methodology, TsAnalyzer measures the **Total Multiplex Rate**:
*   The bitrate for a program is derived from the *total number of TS packets* delivered in the entire multiplex between that program's PCR markers.
*   This accurately reflects the program's physical bandwidth occupancy, including all overhead and stuffing associated with its timing.

### 2.3 Integrity-First Reset Logic (CC Error Awareness)
Metrology must be verifiable and honest. TsAnalyzer implements a "Discard on Error" policy:
*   **CC Error Sensitivity**: If a Continuity Counter (CC) error is detected within a measurement window, the current calculation is **aborted**.
*   **Rationale**: Packet loss compromises the packet count (numerator), leading to incorrect bitrate reports. Resetting ensures that only contiguous, valid data is used for metrology.

### 2.4 Multi-Program (MPTS) Isolation
To support complex MPTS streams without "Value Overwriting" or "Scaling Spikes":
*   **Isolated Contexts**: Each PCR PID maintains its own `br_est` structure (containing anchors and sync flags).
*   **Independent State Machines**: PCRs from different programs are processed in parallel clock domains.
*   **Aggregation**: The global `pcr_bitrate_bps` is the algebraic sum of all independent program bitrates.

## 3. Comparison & Mapping

| Feature | Industrial Standard | TsAnalyzer Implementation |
| :--- | :--- | :--- |
| **Packet Counting** | Interval-based increment | Global Anchor ($P_{total} - P_{anchor}$) |
| **Sync Check** | PCR pair validation | `br_est.sync_done` flag |
| **Error Handling** | Reset on CC mismatch | Check `last_cc_count` per window |
| **Multi-Program** | State machine per PID | `clock_inspectors[pid].br_est` |

## 4. Conclusion
By aligning with the anchor-based model, TsAnalyzer eliminates software-induced jitter from its statistics. This methodology provides a stable, "linear" bitrate report that matches the performance of high-end hardware probes.
