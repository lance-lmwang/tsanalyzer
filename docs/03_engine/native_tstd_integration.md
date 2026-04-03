# Native T-STD Integration Design (Industrial Asynchronous Edition)
*Broadcast-Grade Implementation Specification & High-Precision Pacing Standard*

## 0. Background & Problem Statement
The legacy FFmpeg `mpegtsenc` implementation is unsuitable for professional broadcast environments due to two critical physical-layer defects:
- **Defect A (Phase Drift)**: Source DTS offsets (e.g., a 26-hour jump from `-stream_loop`) are passed directly to the output, creating a massive discontinuity between physical byte offsets and internal timestamps.
- **Defect B (Burst Pacing Jitter)**: The packet-driven execution model causes "bursty" output where hundreds of TS packets are emitted simultaneously during a single FFmpeg callback, resulting in physical jitter exceeding 200ms.

## 1. Overview & Design Goals
This document defines a production-grade, deterministic T-STD (Transport System Target Decoder) integration for FFmpeg `mpegtsenc`. It evolves from a simple packet-driven scheduler into a **High-Precision Asynchronous Control System** designed for 7x24 mission-critical broadcast chains.

### 1.1 Engineering Constraints & Portability
To maintain FFmpeg community standards, the following portability rules are mandatory:
- **Threading**: Do not use raw `pthread_create`. Use FFmpeg's internal threading abstraction (`libavformat/thread.h` or `libavutil/thread.h`) or wrap with `#if HAVE_PTHREADS`.
- **Timing**: `CLOCK_MONOTONIC_RAW` is Linux-specific. Use `av_gettime_relative()` or provide platform-specific wrappers (e.g., `mach_absolute_time` for macOS).
- **CPU Instructions**: `_mm_pause()` must be guarded by `#if ARCH_X86`. For ARM/AArch64, use `__asm__ volatile("yield")`.

### 1.2 Core Business Goals
- **Strict CBR Output**: Bit-exact constant bitrate via Integral-Actuated Bitrate PLL.
- **ES Rate Stability**: High-precision stability for individual Audio/Video PIDs (fluctuation < 64kbps) to satisfy broadcast standards.
- **PCR PLL Excellence**: Frequency-locked PIF controller with spectrum simulation for sub-100ns accuracy.
- **T-STD Compliance**: Continuous Leak CPB model with TR 101 290 P1/P2 automated verification.
- **Livelock Prevention**: Cooperative scheduler with emergency anchoring and time-sliced execution.
- **Energy Conservation**: Long-term bandwidth stability via physical-byte-based Token headroom accounting.

---

## 2. Core Architectural Concepts & Analysis

### 2.1 Zero-Drift Virtual System Time Clock (V-STC)
Unlike traditional multiplexers that rely on system wall-clocks, this architecture locks the virtual clock strictly to the physical byte output (emitted).
`v_stc = stc_offset + (total_bytes_emitted * 8 * 27000000) / mux_rate`
**Crucial:** `total_bytes_emitted` must be updated only in the Pacer thread after `avio_write`. This ensures PCR values are perfectly aligned with their actual physical emission time.

### 2.2 PCR PLL Closed-Loop (Cross-Domain Calibration)
To achieve sub-microsecond jitter, the PLL must bridge the Logical Domain and Physical Domain.
- **Expected**: `v_stc` (The scheduled time).
- **Measured**: `wallclock_to_27m(now) - mux_delay_27m` (The actual physical time).
**Analysis:** By measuring against the physical wall-clock, the PLL can actively correct pacing jitter introduced by the OS scheduler.

** High-Precision Pacer Framework:**
```text
                +--------------------+
AVPacket -----> |   T-STD Scheduler  |
                |  (v_stc domain)    |
                +--------+-----------+
                         | TS packet
                         v
                +--------------------+
                |   Ring Buffer      |  (lock-free)
                +--------+-----------+
                         |
                         v
                +--------------------+
                |  PACER THREAD      |  (wallclock)
                |  CLOCK_MONOTONIC   |
                +--------+-----------+
                         |
                         v
                      avio_write()
```

- **Scheduler**: Decision based on `v_stc`.
- **Pacer Thread**: Emission based on `CLOCK_MONOTONIC_RAW` + `Busy-Wait` + `PLL`.
- **Bridge**: SPSC Lock-free Ring Buffer.

### 2.2 PCR PLL Closed-Loop & Stability Control
Open-loop slot alignment causes jitter spikes during I-frame bursts.
**Evolution:** Implement a PI (Proportional-Integral) controller with dual-clamping.
**Engineering Realism:** Introduce a jitter spectrum model (high-freq noise + low-freq drift) to simulate real-world physical links. In software-only mode, the PCR is perfectly aligned with `d_stc` using a simplified shift-gain controller:
`measured = v_stc - (mux_delay * 27000000LL) + jitter_sim(drift, noise)`
`PhaseError = measured - expected`
`Correction = (PhaseError >> PLL_KP_SHIFT) + (Integral >> PLL_KI_SHIFT)`
**Crucial:** `PCR_Output = expected + Correction`. This ensures absolute frequency stability without measurement contamination.

### 2.3 Global Token Pool & Fairness (Time-Consistent)
Borrowing mechanisms must be time-consistent. Mixing continuous generation with discrete packet events leads to subtle bandwidth inflation.
**Evolution:** Transition from "Reservoir" to "Instant Rate Limiter" model. The global pool represents the physical bandwidth "headroom".
`null_budget += (expected_total_bytes(v_stc) - actual_bytes_written)`
This ensures the global pool acts as a strict rate-limiter, maintaining long-term CBR integrity by preventing bandwidth "savings" from being spent all at once.

### 2.4 Robust Arrival Gate (Lag-Limited Soft Landing)
Aggressive reset of packet release times causes buffer spikes and bitrate surges during DTS discontinuities (e.g., encoder flushes).
**Evolution:** Implement a lag-limited soft landing with adaptive compensation and emergency reset.
`next_arrival_ts = FFMAX(next_arrival_ts, v_stc + PACKET_TIME_27M(mux_rate))`
This spreads the release of "late" packets across a short window, smoothing out the bitrate impact while preventing infinite latency accumulation.

### 2.5 Dual-Loop Bitrate & NULL Control
Relying solely on PID deadlines leads to insertion lag and instantaneous bitrate ripples.
**Evolution:** Introduce a Bitrate PLL (Second Control Loop) to drive NULL packet rhythm as a continuous modulation signal.
`null_budget = av_clip64(null_budget, -BITRATE_THRESHOLD, BITRATE_THRESHOLD)`
If `null_budget >= 188`, the actuator emits a NULL packet. This smooths the bitrate waveform into a near-perfect line.

---

## 3. FFmpeg Integration Strategy

### 3.1 Proposed File Structure
```text
libavformat/
 -- mpegtsenc.c        // Minimal hooks for init, enqueue, and drain
 -- tstd.c             // Core T-STD engine implementation
 +-- tstd.h             // T-STD data structures and function prototypes
```

### 3.2 AVOption Design (Production Set)
```c
{ "mpegts_tstd_mode", "scheduler mode (0: Legacy, 1: Strict CBR, 2: Token+EDF)", OFFSET(tstd_mode), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 2, E },
{ "mpegts_tstd_token_floor", "PID-level negative borrowing limit", OFFSET(tstd_token_floor), AV_OPT_TYPE_INT, { .i64 = -1024 }, -65536, 0, E },
{ "mpegts_tstd_reorder", "DTS reorder window delay (seconds)", OFFSET(tstd_reorder), AV_OPT_TYPE_DOUBLE, { .dbl = 0.1 }, 0, 1.0, E },
{ "mpegts_tstd_pll", "enable PCR PLL simulation", OFFSET(tstd_pll), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, E },
{ "mpegts_tstd_video_rx", "Video Rx,n rate (bps)", OFFSET(tstd_video_rx), AV_OPT_TYPE_INT64, { .i64 = 0 }, 0, INT64_MAX, E },
```

### 3.2.1 Operational Examples (Benchmark Command)
To verify implementation compliance, use the following standardized benchmark command:
```bash
ffmpeg -v debug -stream_loop -1 -i input.mp4 -t 30 \
    -c:v libx264 -b:v 1600k -maxrate 1600k -bufsize 1600k -nal-hrd cbr \
    -muxrate 2000000 -max_delay 900000 -mpegts_tstd_mode 1 output.ts
```
*Note: This command evaluates both Phase Drift (via -stream_loop) and Pacing Jitter (via high-precision audit).*

### 3.3 Lifecycle & Integration Hooks
The T-STD engine intercepts `mpegts_write_packet_internal` to replace the legacy system-clock-based pacing with a deterministic virtual-clock control loop.

**Initialization (`ff_tstd_init`):**
- **Context Allocation**: Allocates `TSTDContext` and links it to `MpegTSWrite`.
- **Constraint Compliance**: Enforces `TSTD_MAX_FIFO_SIZE = 1MB` and initial `v_stc = -1`.
- **Global Pool**: Initializes the Global Token Pool (integrated into Bitrate PLL null_budget).
- **Hierarchical Scaffolding**: Iteratively allocates `TSTDProgram` and `TSTDPidState` structures.
- **Resource Allocation**: Initializes `AVFifoBuffer` for TS packets and `au_events` for T-STD removal tracking using `av_fifo_alloc2`.

**Teardown (`ff_tstd_deinit`):**
- **Drain & Join**: Signals `stop_pacer`, joins the pacer thread, and ensures all packets in the SPSC are emitted.
- **Deep Clean**: Recursively traverses the hierarchy to free all programs and PID states.
- **Queue Cleanup**: Explicitly flushes and frees all `AVFifoBuffer` and AU event queues to ensure zero memory leakage.
- **Pointer Nulling**: Clears the `mpegts_tstd_ctx` reference in the parent structure.
- **Teardown Safety**: Implementation must handle emergency exits (e.g., SIGINT). Ensure the main FFmpeg thread is unblocked from `spsc_push` if the pacer thread terminates prematurely. Use ASAN/Valgrind to verify zero memory leakage in the SPSC ring buffer.

**Interception Hook Logic:**
```c
static int mpegts_write_packet_internal(AVFormatContext *s, AVPacket *pkt) {
    MpegTSWrite *ts = s->priv_data;

    if (ts->tstd_mode > 0) {
        // 1. Buffer packet and calculate soft-landing constraints
        ff_tstd_enqueue(ts, pkt);

        // 2. Drive the control system loop until the queue is drained below safety thresholds
        // Note: Production implementation uses Modulo-8 Throttling for ff_tstd_drive()
        while (ff_tstd_needs_drain(ts)) {
            ff_tstd_step(s);
        }
        return 0;
    }
    // Fallback to legacy best-effort multiplexing
    return legacy_mpegts_write_packet(s, pkt);
}
```

---

## 4. System Constants & Thresholds
To ensure stability across various mux rates, the following physical constraints and tuning parameters are defined:

| Constant | Recommended Value | Engineering Rationale |
| :--- | :--- | :--- |
| **MAX_ARRIVAL_LAG** | `27MHz / 10` (100ms) | Prevents infinite latency accumulation during DTS jumps. |
| **BITRATE_THRESHOLD** | `188 * 10` bytes | Integral threshold to trigger NULL insertion for CBR stability. |
| **TSTD_HIGH_WATERMARK** | `512 KB` | Threshold to trigger emergency clock anchoring and drain. |
| **TSTD_MAX_STEPS** | `32` | Maximum packets processed per `ff_tstd_drive` call. |
| **PLL_PHASE_CLAMP** | `27MHz / 100` (10ms) | Protects decoders from PCR step-jumps during resets. |
| **PLL_FREQ_CLAMP** | `27000` (1ms/s drift) | Bounds the maximum allowable frequency correction rate. |
| **SCORE_DAMPING_SHIFT** | `1` (0.5 weight) | Lyapunov D-term damping factor balancing response and oscillation. |
| **PLL_KP_SHIFT** | `3` (1/8 gain) | Proportional gain bit-shift for phase tracking. |
| **PLL_KI_SHIFT** | `7` (1/128 gain) | Integral gain bit-shift for frequency tracking. |
| **PCR_MARGIN_TICKS** | `(188 * 8 * 27M) / rate` | Minimum lead-time to trigger a PCR slot reservation. |

---

## 5. T-STD Core Data Structures

### 4.1 PID State & Classes
```c
typedef enum {
    TSTD_CLASS_PCR = 0,
    TSTD_CLASS_PSI = 1,
    TSTD_CLASS_VIDEO = 2,
    TSTD_CLASS_AUDIO = 3
} TSTDClass;

typedef struct TSTDPidState {
    int pid;
    TSTDClass cls;
    AVFifoBuffer *ts_fifo;     // TS packet storage

    // --- Leaky Token Bucket (Traffic Shaping) ---
    int64_t tokens_bytes;      // Current available tokens
    int64_t bucket_size_bytes; // BSn capacity
    int64_t token_floor;
    int64_t refill_rate_bps;   // Rx,n rate

    // --- AU-Level Model (Continuous Leak Simulation) ---
    int64_t buffer_level;      // Current occupancy in bytes (Decoder CPB)
    int64_t violation_overflow;
    int64_t violation_underflow;
    AVFifoBuffer *au_events;   // Queue of {dts, size} for discrete removal

    int64_t last_update_ts;
    int64_t next_arrival_ts;
    int64_t last_dts;
    int64_t lag_accumulated;   // Tracking latency creep
} TSTDPidState;
```

### 4.2 MPTS Program State (with PCR PLL)
```c
typedef struct TSTDPcrPLL {
    int64_t phase_error;
    int64_t freq_error;
    int64_t last_pcr;
} TSTDPcrPLL;

typedef struct TSTDProgram {
    int program_number;
    int pcr_pid;
    TSTDPidState *pcr_pid_state;
    TSTDPcrPLL pll;

    int64_t target_bitrate_bps;
    int64_t actual_bytes_sent;
    int64_t last_debt_ticks;    // For Velocity damping
    int     priority_weight;

    int nb_pids;
    TSTDPidState **pids;
    int64_t pcr_period;
    int64_t next_pcr_ts;
} TSTDProgram;
```

### 4.3 Global Context (The Physical Controller)
```c
typedef struct TSTDContext {
    int64_t v_stc;             // Physical Master Clock
    int64_t d_stc;             // Decoder Removal Clock
    int64_t mux_rate;
    int64_t total_bytes_written;

    int64_t global_tokens;     // Physical Bandwidth Pool
    int64_t null_budget;       // Bitrate PLL Integral Actuator

    int nb_programs;
    TSTDProgram **programs;
    int nb_all_pids;
    TSTDPidState **all_pids;
} TSTDContext;
```

---

## 6. T-STD Core Mechanics

### 5.1 Enqueue & Soft-Landing Protection
```c
static int ff_tstd_enqueue(MpegTSWrite *ts, AVPacket *pkt) {
    TSTDContext *tstd = (TSTDContext *)ts->mpegts_tstd_ctx;
    TSTDPidState *pid = tstd_get_pid(ts, pkt->stream_index);

    if (pkt->dts != AV_NOPTS_VALUE) {
        int64_t dts_27m = av_rescale(pkt->dts, 27000000, AV_TIME_BASE);

        // Emergency Reset on backward jump or excessive latency drift
        if (dts_27m < pid->last_dts || (pid->next_arrival_ts - tstd->v_stc) > MAX_ARRIVAL_LAG) {
            pid->next_arrival_ts = tstd->v_stc;
        } else {
            // Soft-Landing: push next arrival forward to avoid bitrate bursts
            pid->next_arrival_ts = FFMAX(pid->next_arrival_ts, tstd->v_stc + PACKET_TIME_27M(tstd->mux_rate));
        }
        pid->last_dts = dts_27m;
    }
}
```

### 5.2 Emergency Anchoring (Anti-Deadlock)
If `av_fifo_size(ts_fifo) > HIGH_WATERMARK` and `v_stc < 0`, the system forces `v_stc = 0` to break the initial dead-lock and start the physical clock even if no DTS has been encountered.

### 5.3 Deterministic Model Update (Time-Consistent)
```c
static void tstd_update_model(TSTDContext *tstd, TSTDPidState *pid) {
    int64_t delta_ticks = tstd->v_stc - pid->last_update_ts;
    if (delta_ticks <= 0) return;

    int64_t gen = (delta_ticks * pid->refill_rate_bps) / (27000000LL * 8LL);
    pid->tokens_bytes = FFMIN(pid->bucket_size_bytes, pid->tokens_bytes + gen);

    /* NOTE: null_budget double integration removed. Handled by single-integrator in step() */
    pid->last_update_ts = tstd->v_stc;
}
```

### 5.4 Lyapunov-Stable Scoring (Damped D-Term)
```c
static int64_t tstd_program_score(TSTDContext *tstd, TSTDProgram *prog) {
    int64_t expected_bytes = av_rescale(tstd->v_stc, prog->target_bitrate_bps, 27000000LL * 8LL);
    int64_t debt_bytes = expected_bytes - prog->actual_bytes_sent;

    // Normalize to 27MHz ticks dimension for linear combination
    int64_t debt_ticks = (debt_bytes * 8 * 27000000LL) / prog->target_bitrate_bps;
    int64_t d_debt_ticks = debt_ticks - prog->last_debt_ticks;
    prog->last_debt_ticks = debt_ticks;

    // Lyapunov Scoring: minimize debt and velocity (d_debt)
    // Applying damping shift to stabilize multi-program arbitration
    return debt_ticks - (d_debt_ticks >> SCORE_DAMPING_SHIFT) + (prog->priority_weight * 1000);
}
```

### 5.5 PCR PLL with Spectrum Simulation
```c
static void tstd_write_pcr(MpegTSWrite *ts, TSTDContext *tstd, TSTDProgram *prog) {
    int64_t expected = prog->next_pcr_ts;
    /* REVOLUTION: Measured time MUST come from Physical Domain (Wall-clock) */
    int64_t measured = wallclock_to_27m(get_ns_raw()) - (ts->mux_delay * 27000000LL);

    int64_t phase_error = measured - expected;
    phase_error = av_clip64(phase_error, -PLL_PHASE_CLAMP, PLL_PHASE_CLAMP);

    prog->pll.phase_error = phase_error;
    // Integral term: accumulates phase error to correct frequency drift
    prog->pll.freq_error += (phase_error >> PLL_KI_SHIFT);
    prog->pll.freq_error = av_clip64(prog->pll.freq_error, -PLL_FREQ_CLAMP, PLL_FREQ_CLAMP);

    int64_t correction = (phase_error >> PLL_KP_SHIFT) + prog->pll.freq_error;

    ff_mpegts_insert_pcr_direct(ts->pb, prog->pcr_pid, expected + correction);
    prog->next_pcr_ts = expected + prog->pcr_period + correction;
}
```

### 5.6 Hierarchical Scheduler Logic (Priority Decision Tree)
```c
static TSTDPidState* tstd_pick_ready_pid(TSTDContext *tstd, TSTDProgram **out_prog) {
    // L0: Absolute Time Priority (PCR Alignment)
    // Check if any program has reached its scheduled PCR injection point.
    // PCR injection always takes precedence over payload to maintain clock domain integrity.
    for (int i = 0; i < tstd->nb_programs; i++) {
        TSTDProgram *prog = tstd->programs[i];
        if (tstd->v_stc >= prog->next_pcr_ts - PCR_MARGIN_TICKS) {
            *out_prog = prog;
            return prog->pcr_pid_state;
        }
    }

    // L1: Mandatory Compliance (PSI/SI Table Timer)
    // Ensure PAT/PMT/SDT repetition intervals (e.g., 400ms) are satisfied.
    // These bypass Token bucket constraints to ensure bitstream decodability.
    for (int i = 0; i < tstd->nb_programs; i++) {
        for (int j = 0; j < tstd->programs[i]->nb_pids; j++) {
            TSTDPidState *pid = tstd->programs[i]->pids[j];
            if (pid->cls == TSTD_CLASS_PSI && tstd->v_stc >= pid->next_arrival_ts) {
                *out_prog = tstd->programs[i];
                return pid;
            }
        }
    }

    // L2: Program Arbitration (Lyapunov-Stable Selection)
    // Select the program with the highest 'debt' (most behind schedule).
    TSTDProgram *best_prog = pick_highest_score_program(tstd);
    if (!best_prog) return NULL; // System is caught up or all PIDs are arrival-gated/token-starved
    *out_prog = best_prog;

    // L3: PID Selection (EDF + Arrival Gate + Token Floor)
    // "Arrival Gate Open" condition: (av_fifo_size(pid->fifo) >= 188 && v_stc >= next_arrival_ts)
    TSTDPidState *best_pid = NULL;
    int64_t earliest_dts = INT64_MAX;

    for (int i = 0; i < best_prog->nb_pids; i++) {
        TSTDPidState *pid = best_prog->pids[i];

        if (av_fifo_size(pid->ts_fifo) >= 188 &&
            tstd->v_stc >= pid->next_arrival_ts &&
            pid->tokens_bytes >= pid->token_floor) {

            // Earliest Deadline First (EDF) based on Essence DTS
            if (pid->last_dts < earliest_dts) {
                earliest_dts = pid->last_dts;
                best_pid = pid;
            }
        }
    }
    return best_pid;
}
```

### 5.7 CPB Continuous Leak Model
```c
static void tstd_update_tbn(TSTDContext *tstd, TSTDPidState *pid) {
    // Continuous Leak Simulation: consumed based on playback rate
    int64_t consumed = (PACKET_TIME_27M(tstd->mux_rate) * pid->refill_rate_bps) / (27000000LL * 8LL);
    if (pid->buffer_level > consumed) pid->buffer_level -= consumed;
    else pid->buffer_level = 0;
}
```

---

## 7. Master Driver Loop (Double Closed-Loop)

```c
void ff_tstd_step(AVFormatContext *s) {
    /* v_stc is updated in Pacer thread based on physical emission */
    
    // [Control Loop 1] Bitrate PLL: Single Integrator based on byte deviation
    tstd->null_budget = expected_total_bytes(tstd->v_stc) - tstd->total_bytes_queued;
    tstd->null_budget = av_clip64(tstd->null_budget, -BITRATE_THRESHOLD, BITRATE_THRESHOLD);

    if (tstd->null_budget >= 188) {
        write_null_packet(s);
        tstd->null_budget -= 188;
        return;
    }

    // [Control Loop 2] Hierarchical Scheduler
    pid = tstd_pick_ready_pid(tstd, &selected_prog);

    if (pid) {
        write_payload_packet(s, pid);
    } else {
        write_null_packet(s);
    }

    finalize_step(tstd); // Log: [T-STD] STC, PID, TOK, GTOK, BUF, ACT, ERR, VIOL
}
```

---

## 8. Tail Closure (Compliance Drain)
The drain process ensures the bitstream remains CBR-compliant until the final packet is emitted.
```c
void ff_tstd_drain(AVFormatContext *s) {
    MpegTSWrite *ts = s->priv_data;
    TSTDContext *tstd = (TSTDContext *)ts->mpegts_tstd_ctx;
    // Bounded drain by the physical buffer lead-time window
    int64_t drain_end_stc = tstd->v_stc + (ts->mux_delay * 27000000LL);
    int max_steps = tstd->mux_rate / (188 * 8);

    while (tstd->v_stc < drain_end_stc && has_pending_data(tstd) && steps++ < max_steps) {
        ff_tstd_step(s);
    }
}
```

---

## 9. Real-time Integration & Flow Control (V26.1 Runtime-Safe Standard)

To ensure safe integration with FFmpegs event-driven muxing pipeline and to avoid livelock or starvation conditions, the T-STD scheduler is implemented as a cooperative, time-sliced control loop.

The following constraints define a runtime-safe, analyzer-compliant behavior model.

### 9.1 Time-Sliced Scheduler (Non-blocking Drive)
The `ff_tstd_drive()` function operates as a bounded, non-blocking scheduler, not as a full draining engine.
*   **Constraint**: `TSTD_MAX_STEPS_PER_CALL = 32`
*   **Behavior**: Each invocation processes a limited number of TS packets and then returns control to the caller.
*   **Rationale**: Prevents monopolizing the calling thread and avoids livelock when upstream continues producing packets.

### 9.2 Backpressure Handling (High-Watermark Fallback)
To avoid unbounded FIFO growth under strict timing constraints, a controlled fallback mechanism is introduced.
*   **Trigger**: `av_fifo_size(ts_fifo) > HIGH_WATERMARK` (recommended: 512 KB)
*   **Action**: A scheduling step (`tstd_step()`) is executed regardless of timing constraints.
*   **Rationale**: Ensures forward progress under pathological conditions and prevents encoder-side blocking.

### 9.3 Drive Throttling (Temporal Decoupling)
The scheduler invocation frequency is decoupled from PES injection rate.
*   **Strategy**: Use a modulo-based trigger (e.g., `counter & 7 == 0`)
*   **Behavior**: `ff_tstd_drive()` is invoked periodically rather than per packet.
*   **Rationale**: Reduces jitter amplification and approximates a discrete-time system over an event-driven pipeline.

### 9.4 Bounded Drain (Termination Safety)
The final drain phase is bounded to prevent infinite scheduling loops.
*   **Limit**: `max_steps ~ mux_rate / (8 * 188)` (~ 1 second of stream time)
*   **Behavior**: Drain terminates when either:
    1.  FIFOs are empty, or
    2.  step limit is reached
*   **Rationale**: Guarantees termination even if clock convergence conditions are not fully satisfied.

### 9.5 Deterministic Progress Guarantee
The scheduler guarantees forward progress under all conditions:
*   At least one TS packet (payload or NULL) is emitted per `tstd_step()`
*   FIFO occupancy is monotonically reduced under sustained backpressure
*   No cyclic dependency exists between: DTS gating, token availability, and scheduler selection.

### 9.6 Threading Model (Explicit Constraint)
The current implementation is:
*   Single-threaded, cooperative scheduling model
*   Runs inside FFmpeg muxing thread
*   Does not introduce locks or blocking primitives
*   Relies on time slicing instead of concurrency

Future extensions MAY introduce a dedicated scheduler thread, but this is explicitly out of scope for the current design.

## 10. DVB-Grade Analyzer Specification (TR 101 290)

The analyzer tool must verify the following industrial parameters:

### 10.1 Priority 1 (Level 1)
- **PAT/PMT Repetition**: Intervals <= 500ms.
- **CC Error**: Continuity counter continuity and ordering.
- **Sync Loss**: Missing 0x47 sync bytes.

### 10.2 Priority 2 (Level 2)
- **PCR Repetition**: Interval <= 40ms.
- **PCR Accuracy**: Max jitter < 500ns (target < 100ns).
- **PCR Frequency**: Spectrum purity of the 27MHz clock.
- **PTS/DTS Errors**: ES removal time cross-check against physical STC.

---

## 11. Telemetry Analysis Tool (`tstd_analyzer.py`)

```python
import re
import pandas as pd
import matplotlib.pyplot as plt

def parse_trace(filename):
    data = []
    # Regex: TOK, GTOK, and ERR are signed integers
    pattern = re.compile(r"\[T-STD\] STC:(\d+) PID:(\d+) TOK:(-?\d+) GTOK:(-?\d+) BUF:(\d+) ACT:(\w+) ERR:(-?\d+) VIOL:(\d+)")
    with open(filename, 'r') as f:
        for line in f:
            m = pattern.search(line)
            if m:
                stc, pid, tok, gtok, buf, act, err, viol = m.groups()
                data.append([int(stc), int(pid), int(tok), int(gtok), int(buf), act, int(err), int(viol)])
    return pd.DataFrame(data, columns=['time', 'pid', 'tokens', 'global_tokens', 'buffer', 'action', 'pcr_err', 'violation'])

df = parse_trace("trace.log")

# Industry standard plots for PCR Accuracy, Bitrate Stability, and TBn Occupancy
# 1. PCR Accuracy Plot
plt.figure(figsize=(12, 5))
plt.plot(df[df['action']=='PCR']['time'], df[df['action']=='PCR']['pcr_err'])
plt.title("PCR Accuracy (Industry Spec < 500ns)"); plt.grid(True); plt.show()

# 2. Instantaneous Bitrate Plot
df['dt'] = df['time'].diff().replace(0, 1)
df['bps'] = (188 * 8 * 27000000) / df['dt']
plt.figure(figsize=(12, 5))
plt.plot(df['time'], df['bps'].rolling(window=100).mean())
plt.title("Instantaneous Bitrate (CBR Stability)"); plt.grid(True); plt.show()

# 3. TBn Buffer Plot
plt.figure(figsize=(12, 5))
for pid in df['pid'].unique():
    sub = df[df['pid'] == pid]
    plt.plot(sub['time'], sub['buffer'], label=f'PID {pid}')
plt.title("TBn Occupancy (Decoder Model)"); plt.legend(); plt.grid(True); plt.show()
```

---

## 12. Advanced Asynchronous Implementation (V30 Pacer Blueprint)

### 12.1 Patch 1:Ring Buffer (Core Infrastructure)
To decouple the scheduler from physical I/O, a synchronized buffer is mandatory.

**Data Structures (tstd.h):**
```c
typedef struct TSTDPacket {
    uint8_t data[188];
} TSTDPacket;

typedef struct TSTDOutputFifo {
    TSTDPacket *pkts;
    int size;
    int r, w;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} TSTDOutputFifo;
```

**Implementation (tstd.c):**
```c
static TSTDOutputFifo *fifo_alloc(int size) {
    TSTDOutputFifo *f = av_mallocz(sizeof(*f));
    f->pkts = av_mallocz(sizeof(TSTDPacket) * size);
    f->size = size;
    pthread_mutex_init(&f->lock, NULL);
    pthread_cond_init(&f->cond, NULL);
    return f;
}

static void fifo_push(TSTDOutputFifo *f, const uint8_t *pkt) {
    pthread_mutex_lock(&f->lock);
    int next = (f->w + 1) % f->size;
    if (next != f->r) {
        memcpy(f->pkts[f->w].data, pkt, 188);
        f->w = next;
        pthread_cond_signal(&f->cond);
    }
    pthread_unlock(&f->lock);
}

static int fifo_pop(TSTDOutputFifo *f, uint8_t *pkt) {
    pthread_mutex_lock(&f->lock);
    while (f->r == f->w)
        pthread_cond_wait(&f->cond, &f->lock);
    memcpy(pkt, f->pkts[f->r].data, 188);
    f->r = (f->r + 1) % f->size;
    pthread_mutex_unlock(&f->lock);
    return 1;
}
```

### 12.2 Patch 2:T-STD Decision Domain Change
The scheduler no longer writes directly to the file. Inside `ff_tstd_step`:
```c
// OLD: avio_write(tstd->avf->pb, pkt, TS_PACKET_SIZE);
// NEW:
fifo_push(tstd->out_fifo, pkt);
```

### 12.3 Patch 3 & 4:High-Precision Pacer Thread
Using `CLOCK_MONOTONIC` and `clock_nanosleep` for absolute time scheduling.

```c
static inline int64_t get_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void *tstd_pacer_thread(void *arg) {
    TSTDContext *tstd = arg;
    AVIOContext *pb = tstd->avf->pb;
    int64_t interval_ns = (188LL * 8 * 1000000000LL) / tstd->mux_rate;
    int64_t next_time = get_ns();
    uint8_t pkt[188];

    while (!tstd->stop_pacer) {
        fifo_pop(tstd->out_fifo, pkt); // Blocks until data ready

        next_time += interval_ns;
        struct timespec ts;
        ts.tv_sec  = next_time / 1000000000LL;
        ts.tv_nsec = next_time % 1000000000LL;

        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
        avio_write(pb, pkt, 188);
        if (tstd->update_total_size) tstd->update_total_size(tstd->avf, 188);
    }
    return NULL;
}
```

### 12.4 Core Upgrade:Industrial SPSC & PLL (V30 Final Form)
For sub-microsecond performance, we upgrade from `pthread_mutex` to **SPSC** and from `nanosleep` to **Busy-wait**.

#### 12.4.1 SPSC Lock-free Ring Buffer (Crucial)
 **Single-Producer (TSTD) + Single-Consumer (Pacer)**

```c
#define RING_SIZE (1 << 16)

typedef struct {
    TSPacket *buf;
    uint32_t size_mask;
    _Atomic uint32_t head;
    _Atomic uint32_t tail;
} SPSCQueue;

// --- Push (with Backpressure) ---
static inline void spsc_push_wait(SPSCQueue *q, const uint8_t *pkt, TSTDContext *tstd)
{
    uint32_t head = atomic_load_explicit(&q->head, memory_order_relaxed);
    uint32_t next = (head + 1) & q->size_mask;

    /* REVOLUTION: Explicit Backpressure instead of silent drop */
    while (next == atomic_load_explicit(&q->tail, memory_order_acquire)) {
        if (tstd->stop_pacer) return;
        // Yield or wait for Pacer to catch up
        sched_yield();
    }

    memcpy(q->buf[head].data, pkt, 188);
    atomic_store_explicit(&q->head, next, memory_order_release);
}

// --- Pop (Lock-free) ---
static inline int spsc_pop(SPSCQueue *q, uint8_t *pkt)
{
    uint32_t tail = atomic_load_explicit(&q->tail, memory_order_relaxed);

    if (tail == atomic_load_explicit(&q->head, memory_order_acquire))
        return 0; // empty

    memcpy(pkt, q->buf[tail].data, 188);
    atomic_store_explicit(&q->tail, (tail + 1) & q->size_mask, memory_order_release);
    return 1;
}
```

#### 12.4.2 High-Precision Timing Sources
 **CLOCK_MONOTONIC_RAW (Stable)**
```c
static inline int64_t get_ns_raw() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}
```
 **TSC (x86 Advance)**
```c
static inline uint64_t rdtsc() {
    unsigned hi, lo;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
```

#### 12.4.3 Busy-wait Pacer (Core Loop)
```c
while (!stop) {
    if (!spsc_pop(q, pkt)) {
        _mm_pause(); continue;
    }
    next_time += interval_ns_corrected;

    // --- Precise Wait (Sub-microsecond Control) ---
    while (1) {
        int64_t now = get_ns_raw();
        int64_t diff = next_time - now;
        if (diff <= 0) break;

        if (diff > 20000) { // >20us -> Yield CPU
            struct timespec ts = {0, diff - 10000};
            nanosleep(&ts, NULL);
        } else { // <20us -> Busy wait
            _mm_pause();
        }
    }
    avio_write(pb, pkt, 188);
}
```

#### 12.4.4 Adaptive Pacer PLL (PI Controller)
```c
typedef struct {
    double error_acc;
    double adjust;
} PacerPLL;

// --- Update Math ---
int64_t expected_time = packet_count * interval_ns;
int64_t actual_time = now - start_time;
int64_t error = actual_time - expected_time;

pll->error_acc += error * 0.001;
pll->adjust = (error * 0.01) + pll->error_acc;

interval_ns_corrected = base_interval_ns - pll->adjust;
```

### 12.5 Patch 5:Termination Lifecycle
```c
ff_tstd_drain(tstd);      // Ensure logic is finished
tstd->stop_pacer = 1;     // Signal pacer
pthread_join(tstd->pacer_tid, NULL); // Wait for physical completion
```

---

## 13. Acceptance Criteria (Definition of Done)
The implementation is considered complete when the following technical gates are passed:
- **Physical Audit Success**: Output TS files or UDP streams must pass high-precision audit (e.g., via `verify_pcr_accuracy.py` or Tektronix MTS4000) with **PCR Jitter < 500ns**.
- **TR 101 290 Compliance**: All Priority 1 and Priority 2 indicators must be **Green (Zero Errors)**.
- **Phase Alignment**: The first PCR in the TS file must be aligned with the physical file start (avoiding the "26-hour drift" issue).
- **CPU Efficiency**: The Busy-Wait Pacer loop must maintain a balance between precision and power. Target CPU overhead for the pacer thread should not exceed 10% of a single core for a 10Mbps stream.
- Stability: Zero memory leaks detected under ASAN/Valgrind after 3600s of continuous high-load muxing.

---

## 14. Critical Implementation Risks (Engineering Safeguards)
Based on real-world engineering experience, the following pitfalls must be actively avoided:

1.  **PCR Slot Misalignment**: The scheduler MUST guarantee that a PCR packet is emitted at the exact logical byte-offset calculated by the model. Delaying PCR emission by even a few TS packets (e.g., waiting for a current PES packet to finish) will trigger "PCR Accuracy Error" in professional analyzers.
2.  **mux_delay Consistency**: `d_stc = v_stc - mux_delay`. While `mux_delay` is a configurable parameter (e.g., `-max_delay 900000`), it MUST remain immutable once the multiplexing session begins. Any runtime fluctuation or inconsistent application of this delay across different PIDs will lead to PCR offset anomalies and measurement jitter.
3.  **NULL Budget Bursting**: The `null_budget` integrator must trigger a NULL packet immediately when it reaches the 188-byte threshold. Allowing the budget to accumulate and then releasing it in a burst will cause an instantaneous "Bitrate Spike," violating CBR compliance.
4.  **PSI/SI Starvation**: Mandatory compliance tables (PAT/PMT/SDT) must always preempt payload data. If the scheduler assigns too much weight to PID essence data, PAT/PMT intervals may drift beyond 500ms, resulting in Priority 1 (P1) audit failures.

