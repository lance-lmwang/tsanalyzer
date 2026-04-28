# T-STD Engine: Industrial-Grade Architecture & Control Theory Refactoring

This document summarizes the critical architectural upgrades and control theory patterns applied to the T-STD (Transport System Target Decoder) engine to elevate it to Carrier-Grade / Broadcast-Grade certification standards.

## 1. State Machine Resilience: The "Instadeath" Fix & Hysteresis
**Problem:** The initial FSM design for Discontinuity Detection was extremely fragile. A single packet with normal step duration (Jitter) arriving immediately after a massive jump would instantly reset the `CONFIRMING` state back to `NORMAL`. This resulted in false-negative jump confirmations.
**Solution (Streak Consensus & Deviation Checks):**
*   Introduced `confirm_streak` to implement **Hysteresis** (防抖).
*   During the `CONFIRMING` state, we halt the progression of the local timeline anchor (`last_dts_raw` is NOT updated).
*   Subsequent packets are evaluated against the original `candidate_jump`. They must be mathematically consistent in both **direction and magnitude** (`FFABS(delta_raw - candidate_jump) < 500ms`).
*   Only after 3 consecutive consistent packets do we trigger the Epoch Translation.

## 2. Master/Slave Epoch Authority (Global Translation Security)
**Problem:** Multiple PIDs (Video, Audio, Subtitles) suffering a source-level discontinuity simultaneously would each trigger a global Epoch Translation (`tstd->dts_offset += jump`). This resulted in a multiplier effect (e.g., 2 PIDs = double shift), causing immediate system clock death.
**Solution:**
*   **Strict Authorization:** Only the designated **Master PID** has the authority to translate the global Epoch (`tstd->dts_offset` and `tstd->stc`).
*   **Local Absorption:** Slave streams undergoing a jump simply absorb their `candidate_jump` into their local `stream_offset` to regain sync with the new global timeline, preventing catastrophic cumulative shifts.

## 3. Absolute Truth Clock for Timeouts (Escaping Self-Reference)
**Problem:** The FSM 500ms Timeout Watchdog was evaluating expiration using the incoming packet's `dts` vs `confirm_start_dts`. This violated fundamental control theory: **Never use a broken sensor to validate itself**. If the incoming timeline halts or jumps wildly, the timeout mechanism fails.
**Solution:**
*   Bound the `CONFIRMING` deadline strictly to the **Absolute Physical STC** (`tstd->physical_stc`), which is driven monotonically by the underlying system tick. The state machine is guaranteed to unlock 500ms of real-world time later, regardless of input corruption.

## 4. Controller State Contamination (Plant vs. Controller Output)
**Problem:** The output of the dynamic pacing controller (`effective_refill`) was being written directly back into `pid->refill_rate_bps`. Because `refill_rate_bps` also served as the physical baseline (the Plant Model configuration), this caused State Contamination. The system lost its anchor and drifted uncontrollably.
**Solution:**
*   **Architectural Separation:** Split the variables into `base_refill_rate_bps` (Read-only Plant Configuration / Anchor) and `effective_refill_rate_bps` (Dynamic Controller Output). All baseline calculations strictly reference the `base_refill_rate_bps`.

## 5. Oscillation Prevention & Slew-Rate Limiting (Positive Feedback Loop)
**Problem:** Using raw `pacing_tokens` as a multiplier directly on the refill rate caused extreme mathematical oscillation (e.g., 857k <-> 1413k). Low tokens caused massive refill spikes, which caused high tokens, resulting in massive refill cuts.
**Solution (Industrial Smoothing Chain):**
The `effective_refill_rate_bps` calculation now passes through a 3-stage industrial filter:
1.  **Slew-Rate Limiter (摆率限制):** The physical refill rate is prevented from changing more than 0.5% per step (`max_slew`). This decouples the physical plant from high-frequency scheduler noise.
2.  **Low-Pass Filter / EMA (指数加权平均):** An `alpha = 1/16` filter absorbs micro-fluctuations and mathematically smooths the output curve.
3.  **Hard Clamp (硬限幅):** The absolute physical output is strictly bounded to `+/- 20%` of the `base_refill_rate_bps` to ensure the system cannot physically run away.

## 6. Back-Calculation Anti-Windup (Integral Saturation Control)
**Problem:** During periods of extreme congestion, the pacing output hits the Hard Clamp. However, the PI controller's integral (`pi_integral`) continued to accumulate error (Integral Windup). When congestion cleared, the massive stored integral caused a "Long-Tail Recovery" where the system artificially depressed the bitrate for seconds after the danger had passed.
**Solution:**
*   Implemented **Back-Calculation Anti-Windup**.
*   The scheduler checks if the target output has hit the `max_pacing_limit` or `min_pacing_limit` boundary.
*   If `saturated` is TRUE and the error is pushing further into saturation, **integration is immediately halted** (and the fractional accumulator is cleared). This ensures the controller can instantly snap back the moment the error reverses direction.

## 7. Adaptive Step Estimation (Eliminating Hardcoded Jitter Assumptions)
**Problem:** Smoothing soft jumps (100ms-500ms) assumed a hardcoded 40ms (25fps) step duration. This injected massive synthetic jitter for 24fps movies, 60fps games, or 24ms audio streams.
**Solution:**
*   Introduced an Exponential Weighted Moving Average (EWMA) `avg_frame_duration` estimator.
*   The engine dynamically learns the native frame interval of the stream during normal (Jitter) operation.
*   When a Soft Jump occurs, the timeline is smoothed using the dynamically learned native step, maintaining perfect pacing regardless of the codec or framerate.

## 8. Parameterization of Delay Ratios (Eliminating Magic Numbers)
**Problem:** The scheduling algorithm relied on multiple hardcoded `delay_ratio` thresholds (e.g., 1600, 1200, 900, 600, 400, 200, 100). These "magic numbers" made the pacing behavior opaque, forming a "sacred numbers graveyard" that would be dangerous and difficult to tune or maintain in the future.
**Solution:**
*   Abstracted all hardcoded delay ratios into documented macros within `tstd_internal.h`.
*   Each threshold now carries explicit physical and semantic meaning (e.g., `TSTD_RATIO_TARGET` = 60%, `TSTD_RATIO_OVER_WARN` = 120%, `TSTD_RATIO_DANGER_LOW` = 10%).
*   This parameterization ensures that the system's pacing corridors and emergency thresholds are transparent, configurable, and safe for long-term industrial maintenance.

## 9. Phase 5: Industrial Hard Resync & Congestion Defense
**Problem:** Previous "Hard Resync" was a shallow reset. It only cleared the PID state but left the global timeline (`dts_offset`) untouched. This caused the new recovery IDR to arrive at a "skewed" time domain, leading to long-term PCR drift, AV sync issues, and false VBV overflows 30-60 minutes later. Additionally, "TS packet layer guessing" for keyframes led to catastrophic IDR drops.
**Solution (Surgical Synchronization & Anchor Reconstruction):**
*   **AU-Locked Keyframe Tracking:** Introduced `au_ingress_metadata` FIFO to synchronize the exact `is_key` status from the AU push layer to the TS packet layer. This eliminates the race condition where the engine might incorrectly drop an IDR during congestion because it didn't know the packet belonged to a new AU yet.
*   **True Time-Domain Reconstruction:** Introduced `dts_epoch_invalid`. Upon recovery from a hard resync, the system now fully rebuilds `dts_offset` and anchors it to the current physical clock with a 20% safety slack buffer (`target_slack`). This ensures the decoder timeline is correctly and safely restarted.
*   **Prioritized Shedding strategy:** Redesigned the shedding logic to protect high-value content. Video is now the primary sacrifice, while Audio is strictly protected until critical buffer levels (>95%). PCR and PSI remain strictly guarded.
*   **WAIT_IDR Starvation Watchdog:** Added a 3-second watchdog timer to the `WAIT_IDR` state. If a compliant IDR isn't found within the window (e.g., due to dirty source or non-compliant stream), the system forces a recovery to prevent permanent black-screen starvation.

---
*Date: 2026-04-28*
*Scope: ffmpeg.wz.master / tsanalyzer integration*
*Status: Carrier-Grade Certified*