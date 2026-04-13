# T-STD Implementation Plan

This document defines the detailed execution steps, milestones, and verification strategies for the refactoring of the T-STD engine based on the Headend Grade Architecture Specification.

## 1. Phase 1: Foundation (vSTC & Slot Logic)
**Objective**: Establish an absolute monotonic physical timeline to eliminate clock collapse.
*   **1.1 Monotonic Slot Counter**: Replace event-driven `v_stc` updates with math derived strictly from `total_slots_count`. Implement a **64-bit Fractional Clock** structure `{int64_t base, num, den}` to eliminate integer division truncation and maintain bit-perfect alignment over 30+ day runs.
*   **1.2 Loop Refactor**: Re-engineer `ff_tstd_step` to ensure each iteration corresponds to a precise $\Delta T_{slot}$.
*   **1.3 Fractional Compensation**: Implement PCR alignment math to achieve sub-100ns precision.

## 2. Phase 2: Buffer Model (Continuous Drain CPB)
**Objective**: Realize real-world IRD behavior via linear leak simulation.
*   **2.1 Linear Leak Implementation**: Implement slot-based `Removal_Bits` deduction in the flywheel.
*   **2.2 Rate Derivation**: Link `Allocated_CBR_Rate` strictly to HRD/VBV parameters. For VBR streams, use `maxrate` as the normative leak ceiling or implement a look-ahead window to prevent deadline drift during bitrate peaks.
*   **2.3 Normalization**: Calculate `Normalized_Deadline` and `Fullness` for the scheduler.

## 3. Phase 3: Scheduler (Hierarchical EDF)
**Objective**: Build a mathematically provable decision tree replacing mixed scoring.
*   **3.1 Strict EDF Core**: Use $T_{removal}$ as the absolute primary priority.
*   **3.2 Tier-break Logic**: Use `Fullness` as a secondary arbiter when $\Delta Deadline < \epsilon$.
*   **3.3 Sticky Hard-cap**: Apply limited context-switch reduction. Introduce **Interleaving Depth Clamps** (e.g., max 7 consecutive packets per PID) to prevent large I-frames from starving audio buffers regardless of deadline priority.
*   **3.4 SI/PSI Preemption**: Implement Tier 0 guaranteed slot passage.

## 4. Phase 4: Compliance & Safety (PCR, NULL, EOF)
**Objective**: Finalize TR 101 290 alignment and edge-case handling.
*   **4.1 Forced PCR Injection**: Implement threshold-based PCR forcing independent of payload. Add a safety guard to ensure generated $PCR_{base}$ is strictly earlier than any $DTS$ in the packet payload, maintaining IRD timing requirements.
*   **4.2 Deadlock-free NULLs**: Smart NULL insertion logic.
*   **4.3 Adaptive Clamping**: Window-based physical pacing (2ms~4ms).
*   **4.4 EOF Soft Landing**: Heavy-duty drain logic for large buffer backlogs.

## 5. Verification & Testing Strategy

### 5.1 Unit Verification
*   **Clock Linearity**: Verify $vSTC$ vs File Size ratio over long runs.
*   **Numerical Accuracy**: PCR jitter analysis (< 100ns generation error).

### 5.2 Stress Matrix
*   **Saturation Test (`bug1.sh`)**: 95% load testing to verify duration fidelity.
*   **Low Bitrate Compliance**: Verify PCR intervals (< 40ms) at 100kbps.
*   **Isolation Test**: Cross-service starvation check in multi-program setups.

### 5.3 Industrial Compliance Audit
*   **TR 101 290 P1/P2 Scanning**: High-precision physical bitstream audit.
*   **Pacing Audit**: Verify 1s window fluctuation < 64kbps.

### 5.4 Robustness
*   **Sync Recovery**: 3s DTS jump injection and recovery time audit.
*   **Drain Fidelity**: Duration mismatch audit across hundreds of short segments.
