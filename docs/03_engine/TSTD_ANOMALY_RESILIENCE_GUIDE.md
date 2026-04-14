# T-STD Engine: Anomaly Resilience & Deadlock Prevention

## 1. Executive Summary: The "Jaco" Case Study
During the validation of high-offset carrier-grade streams (e.g., the Jaco test source), the T-STD engine (Mode 1/2) experienced severe **Media Drops** and **Scheduler Deadlocks**, while the Native FFmpeg Muxer (Mode 0) remained stable.

The investigation revealed that the engine's clock virtualization was too tightly coupled to potentially corrupted input timestamps, leading to a state where the engine "locked itself out" of sending packets despite having ample data in its buffers.

## 2. Problem Analysis

### 2.1 Pseudo-Starvation & Burst Ingestion
Broken input sources often exhibit a "Stutter-then-Burst" pattern:
1. **Input Gap**: Corrupted frames or demuxer errors cause a pause in packet delivery (e.g., ~900ms).
2. **Flywheel Trigger**: The T-STD engine detects no input and triggers "Starvation Recovery," attempting to drain buffers to maintain the physical clock.
3. **Massive Burst**: The demuxer suddenly provides a massive amount of backlogged data (e.g., 4MB) to catch up.

### 2.2 The Clock Deadlock (The "Silent" Failure)
The deadlock occurs primarily due to **Backward DTS Jumps**:
- **Clock Misalignment**: If input DTS jumps back (e.g., from 100s to 70s) and the `dts_offset` is not atomically updated, the engine calculates `rel_dts` as a past or negative value.
- **Token Debt**: If the engine forced these "late" packets out, the Token Bucket would fall into massive debt (e.g., `TOK: -114048`).
- **Scheduling Lock**: The scheduler refuses to emit further packets while "repaying" this debt, but since the physical clock (`v_stc`) is stalled waiting for a future DTS that never arrives (due to the backward jump), the engine stops transmitting.
- **Overflow**: Subsequent burst data fills the FIFO, eventually triggering a `MEDIA DROP`.

## 3. Reproduction Strategy (Chaos Simulation)

To reproduce this without massive binary files, we use a **Chaos Simulation Script**:
1. **Normal Phase**: Emit 5 seconds of valid CBR MPEG-TS.
2. **Chaos Point**:
   - Perform a **Logical Rewind**: Force the next DTS to jump back 30 seconds.
   - Inject a **Physical Burst**: Feed 10,000 packets into the muxer in a single call.
3. **Verification**: Observe if the engine drops packets or if the output stream stalls.

## 4. Architectural Hardening (The "Never-Lock" Policy)

To ensure the engine is 100% resilient against corrupted inputs, three "Hardening Pillars" are implemented:

### Pillar A: Independent Physical Flywheel
The physical transmission clock (`v_stc`) must be **decoupled** from the input DTS. Once started, `v_stc` increments monotonically based on physical packet slots (Ticks-per-packet). Input DTS is used *only* to calibrate the `offset`, never to stall the transmission flywheel.

### Pillar B: Symmetric Atomic Re-anchoring
The engine must treat Forward and Backward jumps with equal priority:
- **Detection**: If `abs(input_dts - last_dts) > TSTD_JUMP_THRESHOLD (3s)`, enter `CONFIRMING` state.
- **Action**: On confirmation, perform an **Atomic Reset**:
  1. Synchronize `dts_offset` to the new timeline.
  2. **Wipe Token Debt**: Reset all Token Buckets to 0 or initial levels.
  3. Re-align `v_stc = new_rel_dts - mux_delay`.

### Pillar C: Pressure-Based Panic Preemption
Buffer safety must override bitrate smoothness during anomalies:
- **Panic Trigger**: If any PID's FIFO exceeds **75% occupancy**, enter **Panic Mode**.
- **Panic Policy**: In this mode, the Token Gate is bypassed. The engine will emit packets as fast as the physical `mux_rate` allows until the buffer drops below 50%.

## 5. Engineering Optimizations (v2026.04 Updates)

Following the initial "Never-Lock" policy implementation, additional optimizations were introduced to enhance CBR precision and system reliability:

### 5.1 Dynamic Drain Throttling
- **Concept**: Introduced `throttle_factor` based on FIFO occupancy.
- **Logic**: Dynamically scales the drain rate from 50% to 200% of the allocated CBR rate. This ensures I-frame bursts are absorbed while maintaining strict average bitrate uniformity.

### 5.2 Balanced NULL Emission
- **Concept**: Converted reactive `ACT_NULL` into a debt-based accumulation model.
- **Logic**: Instead of immediate insertion, NULL packet requirements are tracked in `null_debt_bits` and emitted at a steady pace to maintain constant physical bandwidth, reducing measured jitter.

### 5.3 Operational Hardening
- **Memory Clamp**: Enforced a hard 10MB limit on PID FIFO buffers to prevent OOM in high-density multi-channel deployments.
- **Observability**: Added independent `mpegts_tstd_debug` flag. Removed `AV_LOG_TRACE` from hot paths to minimize CPU overhead, elevating critical audit logs to `AV_LOG_INFO`.
- **PCR Sanity**: Replaced silent PCR regression correction with warning-level logging, allowing operations teams to identify source-side clock stability issues without impacting downstream stream continuity.
- **Config Sanity**: Enforced 10kbps `mux_rate` minimum floor to prevent catastrophic calculation errors in the scheduler.
