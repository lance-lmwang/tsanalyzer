# T-STD Engine Post-Mortem & Tuning Guide

This document summarizes the major hidden defects, algorithmic tuning principles, and testing blind spots encountered during the development and validation of the FFmpeg T-STD (Transport Stream System Target Decoder) industrial-grade streaming scheduler.

## I. Why Did Regular Smoke Tests Fail to Catch the Bug?

In the initial development phase, we executed a 30-second `bug.sh` smoke test. We observed that the output duration was exactly `30s` and both audio and video were playable. This led us to falsely believe the T-STD scheduling model was working perfectly. However, during a 5-minute `compare_muxers.sh` stress test, the file experienced an **astounding 52-second duration stretching**, and the **audio abruptly disappeared** midway through playback.

**Analysis of the Missed Detection:**
1. **The Masking Effect of Time Scales:** The smoke test was only 30 seconds long. Over such a short period, the clock drift caused by "precision truncation" and "token limits", as well as the slow accumulation in queues, had not yet reached the critical threshold to trigger a FUSE sync or completely blow up the 512KB FIFO.
2. **Total Duration Masked Single-Track Collapse:** Media analysis tools (like `mediainfo`) report the General Duration based on the longest stream (usually video). In the long test, the video continued to run for 5 minutes, completely masking the fact that the audio track had already "died of starvation and abandonment" at the 1m20s mark.
3. **"Silent Drops" and Blind Padding:** When the FIFO became full, the original system only printed `TRACE`-level logs. During the Drain phase, to wait for the backlogged audio, the system silently inserted a staggering **160,000 NULL packets**. In a short test, this might have only manifested as "a few hundred extra packets", failing to attract attention.

## II. The Five Core "Craters" & Algorithmic Refactoring

### Crater 1: Dynamic Overwrites Destroyed the Static Buffer Model (The Chief Culprit)
* **Symptom:** Scheduling deadlocks triggered instantly when processing I-frames, resulting in massive NULL packet insertion.
* **Root Cause:** Upon receiving a data packet in `ff_tstd_enqueue_dts`, the original code dynamically recalculated `bucket_size_bits` (Token Bucket / EB size) and `refill_rate_bps` based on `delta_pid` (the time difference between two frames). This caused the perfectly reasonable 2MB initial bucket to be overwritten to a tiny ~3KB in an instant! A 3KB bucket cannot even hold a fraction of a 30KB I-frame. This instantly caused "token exhaustion", forcing the scheduler to furiously send NULL packets to buy physical time and restore tokens.
* **Fix:** **Completely removed the dynamic buffer calculation based on single-frame intervals.** The physical buffer model of T-STD is static and rigid. It must strictly adhere to the industrial standards initialized in `ff_tstd_init` (e.g., 2MB for video, 128KB for audio) and must never fluctuate with the instantaneous characteristics of the stream.

### Crater 2: Microsecond Stepping Truncation (The Precision Black Hole)
* **Symptom:** Even in a steady state without bursty large frames, tokens would still chronically run short.
* **Root Cause:** `add_bits = av_rescale(delta_27m, p->refill_rate_bps, TSTD_SYS_CLOCK_FREQ);`. Because the clock steps forward per-packet (about 1.25 milliseconds), the extremely small `delta_27m` produces a fractional remainder of 0.x bits which is truncated (rounded down) by integer division. Over tens of thousands of packet transmissions, these eaten 0.x bits resulted in an overall refill rate lower than the expected bitrate, causing chronic "token starvation".
* **Fix:** Introduced high-precision remainder registers `token_remainder` and `tb_leak_remainder`. By preserving the division remainder for the next cycle, we achieved **absolute zero precision loss** over millions of scheduling operations.

### Crater 3: The EDF Scheduler's "Amnesia" Clamping (The Audio Killer)
* **Symptom:** Video continued to be sent, but audio was pinned in the queue until the FIFO exploded and packets were dropped.
* **Root Cause:** The original scheduler code contained `if (tstd->v_stc > pid->next_arrival_ts) pid->next_arrival_ts = tstd->v_stc;`. When audio fell behind because it was preempted by a video burst, this "clamping" code instantly dragged the audio's expected time to the present. This prevented low-bitrate streams from accumulating "starvation debt (Negative Slack)", causing them to lose every competition against video streams that possessed massive `buffer_boost` advantages.
* **Fix:** **Completely removed the clamping code.** This allows lagging streams to keep their "historical debt", increasing their priority to infinity. This ensures the EDF (Earliest Deadline First) scheduler can fairly and rigorously compensate streams that were previously neglected.

### Crater 4: Rigid Audio Throttling & Drain Avalanche
* **Symptom:** The 5-minute test had 52 seconds of dead silence (pure NULL packets) appended to the end.
* **Root Cause:** The encoder is allowed to generate audio ahead of time. When the Muxer reached the end, a large number of audio packets piled up in the T-STD queue. However, the audio refill rate was rigidly clamped to `bitrate * 1.2` (e.g., 57 kbps). To empty tens of kilobytes of audio queue, the scheduler had no choice but to transmit at the glacial pace of 57kbps, inserting up to 50 seconds of NULL packets to advance the physical time accordingly.
* **Fix:** Relaxed the token refill limit for audio, allowing it to utilize up to **15% of the total Muxrate** as a safe fallback. This ensures that during periods of abundant network bandwidth or the final Drain phase, backlogged audio can be flushed instantly, entirely eliminating the false duration stretching at the tail end.

### Crater 5: Hardcoded PSI/SI Throttling Deadlocks
* **Symptom:** `[T-STD MONITOR] PID 0x0000 WARNING: Buffer 70.1% full!` spamming continuously.
* **Root Cause:** The external Muxer might dump multiple PAT/PMT/SDT packets into the FIFO in a single loop cycle while processing a massive video frame. However, the internal T-STD scheduler was rigidly restricting the output to "one PSI packet every 200ms". This caused a severe supply-demand imbalance, instantly blowing up the PSI queue.
* **Fix:** Removed the strict frequency censorship of PSI packets from the scheduler. As high-priority metadata, as long as PSI exists in the FIFO and its miniature TB transport buffer is not full, the scheduler will transmit it with the highest priority immediately.

### Crater 6: The "Observer Illusion" (Double-Counting Physical Counters)
* **Symptom:** PCR jitter exceeded 4 seconds, and low-bitrate streams reported an impossible **47Mbps** fluctuation in a 1.2Mbps pipe.
* **Root Cause:** The core physical counter `total_bytes_written` was being incremented in two separate places within the same emission cycle. Because the virtual clock `v_stc` is strictly derived from this counter, the internal timeline was moving at **2x physical speed**.
* **Chain Reaction:**
    1. The engine thought time had passed twice as fast as it actually had.
    2. This caused the `v_stc` to outrun source DTS rapidly, triggering constant `DRIVE FUSE` re-anchoring.
    3. During re-anchoring, the 3-second "time jump" instantly generated millions of virtual tokens, causing a massive packet burst.
    4. Audit tools, observing the corrupted STC timestamps in the log, calculated astronomical bitrates.
* **Fix:** **Unified all physical state updates into a single atomic block at the end of the packet emission cycle.** Ensured that logging occurs strictly *after* the physical state update to maintain causal integrity for audit tools.

### Crater 7: Double-Feedback Oscillation (The Hunting Effect)
* **Symptom:** Output bitrate wildly oscillated (830k-980k) during the first 15 seconds of transmission, causing "hunting" patterns on bitrate analyzers.
* **Root Cause:** Resonance between two independent feedback loops. Both the global token generator (`refill_rate_bps`) and the PID-specific consumer (`pacing_tokens`) were reacting to the same delay error. When one speeded up, the other accelerated further, leading to overshoot and subsequent violent braking.
* **Fix:** **Unified Single-Point Control.** Locked the global `refill_rate` to a stable 101% baseline and moved all dynamic regulation to the `pacing_tokens` consumption layer with a tiered damping model.

### Crater 8: Shared PID CC Synchronization (The ES Layer Killer)
* **Symptom:** Decoder reported "Packet Corrupt" and PES layer integrity failed, despite correct VBV levels.
* **Root Cause:** In the standard "Shared PID" mode (PCR on Video PID), the T-STD engine inserted PCR-only packets without knowing the current Continuity Counter (CC) used by the upstream Muxer. This caused random CC jumps and gaps in the PES sequence.
* **Fix:** **Implemented Transparent CC Sniffing.** The T-STD engine now snoops the CC of every payload packet emitted and applies that value to interleaved PCR-only packets (without incrementing). This makes the T-STD pacing layer completely transparent to the TS continuity sequence.

### Crater 10: Variable Scope Shadowing (The Silent Emission Killer)
* **Symptom:** Audit results reported `MEANk: 0.00` despite healthy VBV telemetry logs.
* **Root Cause:** A sub-block within the scheduler re-declared the `pid` or `prog` variables, shadowing the outer scope used by the emission logic. The scheduler selected a packet, but the emission logic saw a NULL pointer.
* **Fix:** **Flattened Decision Logic.** Removed redundant curly-brace scoping within `ff_tstd_step_internal` and explicitly initialized result variables at the top of the function.

### Crater 11: Non-Exclusive Scheduler Deadlock
* **Symptom:** Video packets were selected but immediately overwritten by NULL packets in the same slot.
* **Root Cause:** The scheduling tree was non-exclusive. If a PCR packet was due, it set `action = ACT_PCR_ONLY`, but the subsequent ES check didn't see the state change and executed `ACT_NULL` logic, leading to structural chaos.
* **Fix:** **Strict Hierarchical if-else-if Tree.** Forced the scheduler into a mutually-exclusive path: `PCR -> ES -> SI -> NULL`. A slot can only be one thing.

### Crater 12: MuxDelay vs. Pacing Corridor Mismatch
* **Symptom:** In 0.7s delay configs, VBV would underflow; in 1.5s configs, score was high.
* **Root Cause:** A fixed ±2.5% pacing corridor is physically incompatible with varying buffer depths. A shallow buffer needs more "bandwidth torque" to survive.
* **Fix:** **Delay-Adaptive Control Law.** Dynamically scaled PI Gain and Corridor limits as a function of `1.0 / muxdelay`.

## III. Diagnostic & Debugging Guide

To ensure future observability without polluting the production environment or causing performance regressions, we have retained key auditing hooks but configured them to the `AV_LOG_TRACE` level.

**Zero-Cost Performance Optimization:**
All heavy diagnostic loops (such as iterating over PIDs to determine *why* a NULL packet was sent) and metric counter increments are wrapped in an `if (av_log_get_level() >= AV_LOG_TRACE)` check.
**This means the diagnostics cost 0 CPU cycles in production.**

To enable these calculations and view the reports, simply run FFmpeg with `-loglevel trace`:

## IV. Expert Review & Critical Redlines (Broadcast Grade)

### 1. Pacing Strategy: Never Abandon the Physical Gate
* **The Pitfall:** Switching to pure "Throughput-Driven" emission (FIFO-only) during cold start to clear backlog.
* **Risk:** Explodes PCR Jitter (PCR_OJ) and violates T-STD transport buffer ($TB_n$) limits, leading to downstream analyzer alarms.
* **The "Professional" Fix:** Implement **Clamped Accelerated Pacing**. Use a "Boost Gear" (e.g., $1.1 \times Muxrate$) but keep every packet anchored to a deterministic slot. When switching gears, use **Remainder Inheritance** ($rem_{new} = rem_{old} \times D_2 / D_1$) to prevent micro-jumps in the STC timeline.

### 2. VBV vs. T-STD: The Phase Mismatch
* **Diagnostic:** 5000ms backlog in a CBR stream indicates the upstream encoder is outputting data at a rate far exceeding the allocated physical bandwidth.
* **Constraint:** If the encoder's VBV model ignores the multiplexer's physical leakage limit, buffer growth is inevitable.
* **Action:** Coordinate with the encoder to enable **Strict CBR** and align `maxrate` with `bufsize`.

### 3. PCR Stretching: The Underflow Trap
* **The Strategy:** Monotonically inject an offset into `vSTC` to "buy time" for massive IDR frames.
* **The Redline:** The total PCR Stretch must **never exceed `mux_delay`** (typically 700ms). Exceeding this limit means the physical delivery time will fall behind the pre-encoded DTS in the PES header, causing catastrophic receiver underflow.