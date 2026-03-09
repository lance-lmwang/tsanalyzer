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
*   **Master PCR PID Locking**: The first identified PCR PID is locked as the "System Time Clock (STC) Master" to prevent clock collisions across programs while maintaining independent bitrate settlements.

## 3. Developer Caveats & Pitfalls (Lessons Learned)

During the implementation of industrial-grade metrology, several critical issues were identified and resolved:

### 3.1 MPTS Clock Collisions
*   **Issue**: In early versions, every PCR PID attempted to update the global logical STC. Minor differences in PCR sampling led to STC jumps, causing all bitrate calculations to spike or drop to zero.
*   **Fix**: Implemented `master_pcr_pid` locking. Only the master PID drives the global STC regression, while other PIDs perform isolated anchor-based settlements.

### 3.2 Clock Domain Hijacking in LIVE Mode
*   **Issue**: Using logical STC for physical bitrate (L2 rate) in `LIVE` mode caused spikes. Processing bursts (e.g., clearing a backlog) appeared as high bitrates (e.g., 600Mbps for a 10Mbps stream) because many packets were processed in a tiny slice of logical time.
*   **Fix**: Physical bitrate MUST use **CLOCK_MONOTONIC** in `LIVE` mode. Logical STC is only used in `REPLAY` mode for determinism.

### 3.3 Minimum Settlement Window
*   **Issue**: Windows smaller than 500ms amplified OS scheduling jitter into bitrate "noise" or "jittery spikes".
*   **Fix**: Enforced a **500ms minimum sampling window**. Bitrate updates are deferred until at least 500ms of real wall-clock time has elapsed.

### 3.4 Aggregate Bitrate Double-Counting
*   **Issue**: `pcr_bitrate_bps` jumped to 20Mbps for 10Mbps streams because the sum included both PCR-calculated mux rates and snapshot-estimated essence rates.
*   **Fix**:
    1.  The PCR engine calculates the high-precision Program Rate.
    2.  The Snapshot logic provides fallback estimation ONLY for non-PCR PIDs.
    3.  `pcr_bitrate_bps` is defined as the sum of all individual PID contributions where PCR-based values take precedence.

### 3.5 Pacer Burst Management
*   **Issue**: TsPacer attempted to "catch up" with line speed (2Gbps+) during file loops or network lag, overwhelming the metrology engine.
*   **Fix**: Reduced queue sizes and implemented "Lag Throttling" where the pacer shifts its base clock forward instead of bursting to catch up.

## 4. Conclusion
By aligning with the anchor-based model and strictly isolating clock domains, TsAnalyzer provides a stable, carrier-grade bitrate report that remains accurate under high CPU load and complex MPTS scenarios.