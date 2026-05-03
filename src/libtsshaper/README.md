# libtsshaper: Professional T-STD Multiplexing & Pacing Engine

`libtsshaper` is a standalone, industrial-grade C library providing hardware-level precision for MPEG-TS traffic shaping and T-STD compliance. It is a 1:1 functional port of the validated native FFmpeg T-STD engine, decoupled into an ABI-stable library.

---

## ⚡ Quick Start for Developers & Maintainers

### 1. The Ground Truth (MUST ALIGN)
Any architectural or constant changes MUST maintain 1:1 functional parity with the native reference.
*   **Bitrate Smoothness**: Delta < 88 kbps (steady-state).
*   **PCR Accuracy**: Jitter < 30 ns (nanosecond-precision).
*   **Score**: `tsanalyzer` Smoothness Score < 350.

### 2. Core Implementation Map
*   **Flywheel Logic**: `src/core/tsshaper.c` (Main entry & logical clock).
*   **EDF Scheduler**: `src/core/tss_scheduler.c` (PID selection & priority).
*   **PI Rate Control**: `src/core/tss_pacing.c` (Token bucket & adaptive gain).
*   **Voter System**: `src/core/tss_voter.c` (Timeline jump & wrap-around logic).
*   **Ingest/IO**: `src/core/tss_io.c` (Packet queuing & priority shedding).

### 3. Build & Integration Workflow
The library is designed to be integrated as a proxy beneath a custom FFmpeg build.

**Prerequisite: FFmpeg Branching**
Before developing or testing changes, you MUST prepare the target FFmpeg repository (`ffmpeg.wz.master`). You must cut a new feature branch from the stable `wz-live` branch to ensure alignment with production standards:
```bash
cd ../ffmpeg.wz.master
git fetch origin
git checkout -b feat_libtsshaper_align origin/wz-live
```

**Step A: Build and Integrate** (Required after any code change)
```bash
# Compiles libtsshaper.a and triggers custom FFmpeg docker build
./scripts/build_ffmpeg_integration.sh
```

**Step B: Execute Regression Suite**
```bash
# Runs high-intensity test phases via the integrated FFmpeg
./scripts/tstd_regression_suite.sh --all
```

---

## 1. Executive Summary
`libtsshaper` is a standalone, high-performance C library designed to provide hardware-level precision for MPEG-TS traffic shaping and T-STD compliance. It transforms loosely-timed VBR streams into strictly compliant CBR streams using a **discrete-event scheduling engine** in a unified 64-bit nanosecond time domain (`tss_time_ns`), ensuring PCR jitter < 30ns and 100% TR 101 290 compliance.

## 2. Triple-Layer Scheduling Architecture
The library treats TS multiplexing as a scheduling problem, decoupling logic from physical emission. This architecture ensures that timing precision is not compromised by heavy processing:

```text
       [ VBR Input ] -> [ Ingest Ring Buffer (SPSC) ]
                                 |
       ┌─────────────────────────▼────────────────────────┐
       │ Layer 1: StatMux (Rate Control & VBV Feedback)   │
       └─────────────────────────┬────────────────────────┘
                                 |
       ┌─────────────────────────▼────────────────────────┐
       │ Layer 2: Interleaver (PID-Aware Priority Arbiter)│
       │          (Weighted Fair Queuing / T-STD Model)   │
       └─────────────────────────┬────────────────────────┘
                                 |
       ┌─────────────────────────▼────────────────────────┐
       │ Layer 3: Pacer (Timing Wheel / Priority Queue)   │
       │          (JIT PCR Rewriting & Precision I/O)     │
       └─────────────────────────┬────────────────────────┘
                                 |
       [ CBR Output ] <- [ High-Precision I/O (sendmmsg) ]
```

## 3. Core Mathematical Models (The 100-Point Logic)

### 3.1 Discrete-Event T-STD Simulation & Compliance
To eliminate cumulative drift inherent in continuous models, buffer levels ($B$) are calculated only at discrete packet arrival/departure points.
The actual DVB/MPEG-TS specification (ISO/IEC 13818-1) defines a 3-stage cascaded T-STD model per Elementary Stream (ES): Transport Buffer (TB), Multiplexing Buffer (MB), and Elementary Stream Buffer (EB). To handle this without parsing PES headers (which is computationally expensive), `libtsshaper` operates in two legal modes:
- **Pass-through Mode (Blackbox):** Assumes the upstream encoder (StatMux) guarantees T-STD compliance. The shaper guarantees overall TS smoothness and does not alter relative packet ordering.
- **Hint Mode (Semantic):** API callers explicitly hint the ES PID type (Video, Audio, PSI/SI), allowing the shaper to apply appropriate predefined $Rbx_n$ and $Rx_n$ leak rate models.

**State Update at $t_{i}$ (Packet Arrival):**
1.  **Leak Phase**: $B(t_{i}^{-}) = \max(0, B(t_{i-1}^{+}) - R_{leak} \cdot (t_{i} - t_{i-1}))$
2.  **Fill Phase**: $B(t_{i}^{+}) = B(t_{i}^{-}) + Size_{packet}$

**Constraints & Safety Margin:**
- $B(t) \le B_{max}$: Violations trigger **Immediate Backpressure**.
- **Proactive Throttle**: A **High-Water Mark (HWM) at 95% of $B_{max}$** is used to signal the StatMux to aggressively reduce the target bitrate ($R_{i}$) before a hard violation occurs.
- $B(t) \ge 0$: Violations trigger **Underflow Protection** (Insert Padding/NULLs to prevent starvation).

### 3.2 VBV-Aware StatMux (Fixed-Point PI Controller)
To maintain strict determinism in the hot path and avoid expensive Context Switch Overheads (FPU state saving), bandwidth allocation $R_{i}$ avoids floating-point math entirely. It uses an $O(1)$ Proportional-Integral (PI) controller based on **Q16.16 Fixed-Point Arithmetic**.

$$R_{i}(t) = K_{p} \cdot C_{i} + K_{i} \cdot (F_{target} - F_{current})$$

- **Proportional Gain ($K_p$)**: Reacts to immediate complexity spikes (e.g., scene cuts).
- **Integral Gain ($K_i$)**: Corrects long-term drift. It incorporates an **Anti-Windup** mechanism (clamping the integral accumulator) to prevent instability during prolonged I-frame bursts.
- **Deadband**: Micro-fluctuations in buffer size are ignored to keep the CBR rock solid.
- **Panic Mode (Asymmetric Penalty)**: If the buffer exceeds the 95% High-Water Mark (HWM), the PI controller instantly bypasses standard math, drops the target bitrate to absolute minimum (`min_bitrate_bps`), and sets an `is_urgent` cross-layer flag. Layer 2 reads this flag and breaks normal polling, aggressively discarding or delaying video packets to protect the T-STD Multiplexing Buffer from overflow.

### 3.3 PCR Regulator & PLL
PCR is a **Timing Function** anchored to the physical emission clock:
- **Interval Regulation**: Forces PCR insertion if no anchor is seen for $> 35ms$. To ensure strict Continuity Counter (CC) compliance and prevent TR 101 290 Priority 1 CC Errors, the shaper inserts a **PCR-only packet** (a TS packet containing only an Adaptation Field without payload). This preserves the original CC sequence.
- **Precision Rewrite**: PCR values are calculated using the *Target Emission Time* ($T_{emit}$) at the nanosecond level.
- **PLL Smoothing & Discontinuity Indicator**: A software PLL filters upstream PCR jitter. Crucially, Layer 1 inspects the Adaptation Field for `discontinuity_indicator == 1` (e.g., ad splicing). Upon detection, the PLL is **immediately reset**, and the first new PCR is passed through without smoothing to prevent decoder crashes.

### 3.4 PSI/SI Cycle Compliance (Priority 1 & 3 Protection)
DVB specification mandates strict transmission cycles for PSI/SI tables: PAT $\le 0.5s$, PMT $\le 0.5s$, and SDT $\le 2s$.
Layer 2 is fully PID-aware: PSI/SI PIDs are granted absolute highest priority. Their emission timing is immunized against any VBV/HWM backpressure or scheduling delays, ensuring they are dispatched cyclically to satisfy TR 101 290 semantics perfectly.

### 3.5 Dynamic PCR Injection & CC State Machine
To guarantee compliance with TR 101 290 Priority 1 and 2 while maintaining lock-free throughput, the engine implements a distinct state machine for Continuity Counters (CC) and PCR generation:

1. **JIT (Just-In-Time) PCR-Only Packet Construction**:
   When Layer 3 (Pacer) detects a PCR interval timeout ($> 35ms$) at the point of emission, it dynamically synthesizes a 188-byte "PCR-only" packet.
   - **Header**: The TS header is constructed with the target PID, `transport_error_indicator = 0`, `payload_unit_start_indicator = 0`, and `transport_priority = 1`.
   - **Adaptation Field Control (AFC)**: Set to `0b10` (Adaptation Field only, no payload).
   - **Continuity Counter (CC)**: DVB specification explicitly mandates that CC **shall not increment** when a packet contains no payload (AFC `0b10` or `0b00`). The engine duplicates the last observed CC for this PID, preserving the state machine for the next payload-bearing packet.

2. **Lock-Free State Maintenance**:
   - `libtsshaper` maintains a highly optimized `pid_context_table` (an array of 8192 context structs) mapping each PID to its last seen CC, PCR value, and timestamp.
   - To avoid stalling the main ring buffer during injection, PCR-only packets are *not* queued in Layer 1 or Layer 2. Instead, they are synthesized in Layer 3's localized buffer at the exact moment of emission and injected directly into the `sendmmsg` I/O batch.

3. **Discontinuity Indicator (DI) Handshake**:
   - When Layer 1 parses an incoming packet with `discontinuity_indicator == 1` in the Adaptation Field, it immediately flags the `pid_context_table`.
   - The PLL is hard-reset. The system bypasses jitter smoothing for the very next PCR received on this PID, adopting it as the new absolute time base.
   - Any scheduled dynamic PCR insertions are temporarily suspended until the new anchor is established to prevent emitting contradictory time bases during a splice.

## 4. Engineering Implementation & Real-Time HAL

### 4.1 High-Precision Pacer & JIT I/O Batching
The final step in the data plane is emitting strictly-timed packets to the network without burning 100% CPU or overwhelming the kernel with soft-interrupt storms. The Pacer thread uses a highly optimized JIT batching mechanism combined with Linux `sendmmsg`:

1. **Three-Stage Precision Sleep Loop**:
   - **Stage 1 ($> 1ms$)**: `poll()` to yield CPU back to the OS and save power.
   - **Stage 2 ($10\mu s - 1ms$)**: `sched_yield()` to minimize context switch latency while keeping the context hot.
   - **Stage 3 ($< 10\mu s$)**: **Hot-spin** using the x86 `PAUSE` instruction (`cpu_relax()`). This prevents CPU pipeline stalls and ensures nanosecond-precision zero-latency wakeups.

2. **SMPTE 2022-2 & sendmmsg Batching**:
   Per industry standards, 7 TS packets (1316 bytes) are encapsulated into one UDP frame to avoid IP fragmentation and optimize MTU. The Pacer dynamically aggregates these UDP frames. It utilizes a **Look-ahead Window (e.g., $50\mu s$)** to group multiple UDP frames (up to 8, or 56 TS packets) whose projected emission times fall within this micro-burst tolerance. It then fires a single `sendmmsg` syscall, offloading the physical pacing to the NIC's DMA engine. This micro-burst is completely within the ±500ns DVB PCR tolerance limit.

3. **Kernel Bypass & Hardware Offload Tricks**:
   - **`connect()` Optimization**: The UDP socket is pre-connected via `connect()`, eliminating kernel route lookups per `sendmmsg` call and effectively turning it into a hyper-fast concurrent `write`.
   - **`SO_MAX_PACING_RATE` (Linux FQ qdisc)**: The socket is optionally configured with `SO_MAX_PACING_RATE` set to the target CBR bitrate. The kernel's Fair Queueing scheduler then natively regulates the micro-burst transmission at the hardware level, providing a free secondary hardware smoothing layer.

### 4.2 Lock-Free SPSC Queue & SIMD Optimization
To achieve > 1 Gbps single-core throughput, the engine implements a zero-lock, zero-syscall Single-Producer Single-Consumer (SPSC) ring buffer heavily optimized for modern CPU architectures:

- **False Sharing Elimination**: Producer and Consumer atomic indices (`head` and `tail`) are rigorously isolated using `alignas(128)` to prevent hardware prefetcher false-sharing across CPU cores.
- **SIMD Alignment & Padding (Zero-Loop Copy)**: Standard TS packets (188 bytes) are internally padded to **192 bytes** (`alignas(32)`). Since 192 is perfectly divisible by 32, this enables the Pacer to use AVX2 intrinsics (e.g., `_mm256_store_si256`) to extract packets from the queue using exactly 6 clock cycles without any `for` loops or slow `memcpy` calls.
- **C11 Acquire-Release Semantics**: The ring buffer strictly avoids expensive sequential consistency (`memory_order_seq_cst`). It relies entirely on `memory_order_acquire` and `memory_order_release` semantics. On x86_64, these compile down to zero-overhead standard memory operations while safely preventing compiler instruction reordering.
- **Metadata Locality**: Packet metadata is maintained in a separate hot-cache array to maximize CPU prefetching efficiency during the Layer 2 scheduling phase.

### 4.3 Clock Source
Strict use of **`CLOCK_MONOTONIC_RAW`** to bypass NTP/PTP time slewing and ensure perfectly linear "TS Time."

## 5. Library Organization & API (ABI Stable)

### 5.1 Project Structure
```text
libtsshaper/
├── include/tsshaper/
│   ├── tsshaper.h    // Tier 1: Public Management API (Opaque Handle)
│   └── types.h       // Shared types (tss_time_ns, stats)
├── src/
│   ├── core/         // Tier 2: T-STD, StatMux, Scheduler Logic
│   └── platform/     // Tier 3: Platform HAL (sendmmsg, affinity, cpu_relax)
├── tests/            // TR 101 290 validation suite & Stress tests
└── examples/         // Integration examples (e.g., with FFmpeg)
```

### 5.2 C API Specification (C11)
```c
#include <stdint.h>
#include <stdbool.h>

typedef int64_t tss_time_ns;
typedef struct tsshaper_ctx tsshaper_t;

#define TSS_IDLE -1  // Return value when no packets are available in the queue

// PID Type Hint for Layer 2 Scheduler and T-STD Model
typedef enum {
    TSS_PID_TYPE_VIDEO,
    TSS_PID_TYPE_AUDIO,
    TSS_PID_TYPE_PSI_SI,  // Absolute highest priority, bypasses VBV/HWM pressure
    TSS_PID_TYPE_PCR_ONLY
} tss_pid_type_t;

// Configuration
typedef struct {
    uint64_t bitrate_bps;
    uint32_t pcr_interval_ms;
    uint32_t io_batch_size;    // Number of packets for sendmmsg (e.g., 7 or 32)
    bool     use_raw_clock;    // Use CLOCK_MONOTONIC_RAW
} tsshaper_config_t;

// Lifecycle
tsshaper_t* tsshaper_create(tsshaper_config_t cfg);
void tsshaper_destroy(tsshaper_t* ctx);

/**
 * Push a packet with an external arrival timestamp and PID type hint.
 * If arrival_ts is 0, the library uses internal CLOCK_MONOTONIC_RAW.
 * @param pid The PID of the packet.
 * @param type Semantic hint to ensure correct scheduling priority and T-STD tracking.
 * @param pkt Pointer to the 188-byte TS packet.
 * @param arrival_ts External timestamp in nanoseconds.
 * @return 0 on success, -1 if the ingest buffer is full.
 */
int tsshaper_push(tsshaper_t* ctx,
                  uint16_t pid,
                  tss_pid_type_t type,
                  const uint8_t* pkt,
                  tss_time_ns arrival_ts);

/**
 * Get the next packet that should be sent (Synchronous Mode).
 * @param out_pkt Buffer to receive the shaped 188-byte packet.
 * @return The projected emission time (T_emit) in ns, or TSS_IDLE if empty.
 */
tss_time_ns tsshaper_pull(tsshaper_t* ctx, uint8_t* out_pkt);

/**
 * Async Mode: Library manages its own Pacer thread and I/O.
 * @param fd The output file descriptor (socket, SRT handle, etc).
 * @return 0 on success.
 */
int tsshaper_start_pacer(tsshaper_t* ctx, int fd);

// Statistics & Monitoring
typedef struct {
    double   current_bitrate_bps;
    uint32_t buffer_fullness_pct;
    double   pcr_jitter_ns;
    uint64_t continuity_errors;
    uint64_t null_packets_inserted;
} tsshaper_stats_t;

void tsshaper_get_stats(tsshaper_t* ctx, tsshaper_stats_t* stats);
```

## 6. Verification & Validation Metrics (100-Point Compliance)
| Metric | Goal | Rationale |
| :--- | :--- | :--- |
| **PCR Accuracy** | < 30 ns | Exceeds TR 101 290 (500ns) requirements; critical for professional satellite uplink. |
| **PCR Interval** | 20ms - 35ms | Ensures fast clock recovery and prevents player buffer oscillations. |
| **CBR Variance** | < 0.001% | Measured over 100ms window; essential for strict bandwidth policing. |
| **T-STD Safety** | Zero Errors | Verified by 24h stress test with Tektronix MTS; ensures no decoder buffer overflows. |
| **Latency** | < 10 ms | Minimizes the impact on end-to-end "glass-to-glass" delay for live sports. |
| **Throughput** | > 1 Gbps | Single-core performance target to enable high-density OTT deployments. |

## 7. CI/CD & Virtual Time Domain Testing
To ensure the "100-Point Compliance" is strictly guaranteed without relying on physical laboratories or expensive analyzers during development, the engine incorporates a fully automated validation harness utilizing a **Virtual Time Domain**:

### 7.1 Fast-Forward HAL & PCAPNG Artifacts
In the CI/CD environment (e.g., GitHub Actions), relying on wall-clock time (`cpu_relax`) is slow and non-deterministic due to container CPU constraints.
- **Clock Interception**: The HAL's `get_time_ns()` is mocked to instantly advance to the next packet's target emission time ($T_{emit}$).
- **I/O Interception**: `sendmmsg()` is mocked to intercept the packet and write it directly to a **PCAPNG** file on disk. The physical emission time ($T_{emit}$) is meticulously recorded in the PCAPNG header at **nanosecond resolution**, preserving the identical timing signature the real network card would have emitted.

### 7.2 Three-Level Verification Pipeline
1. **Level 1: Semantic Compliance (TSDuck)**
   - The generated `output.pcapng` is parsed by TSDuck (`tsp -I pcap ... -P analyze --tr101290`).
   - **Assertion**: CI strictly asserts zero Priority 1/2/3 errors (e.g., CC Continuity errors, PAT/PMT > 500ms).
2. **Level 2: Nanosecond Clock Analysis (Python)**
   - A custom Python script extracts the absolute $T_{pcap}$ nanosecond timestamps from the PCAPNG file and compares them against the decoded $T_{pcr}$ values embedded inside the payload.
   - **Assertion**: $\max(|T_{pcap} - T_{pcr}|) < 30ns$ and PCR intervals strictly bounded between 20ms and 35ms.
3. **Level 3: Commercial-Grade Endorsement**
   - The CI artifact is passed to software-based commercial analyzers (e.g., DekTec StreamXpert or Tektronix MTS) via an offline API to parse the final XML report as the definitive sign-off.

### 7.3 Chaos Engineering (Stress Testing)
- **The I-Frame Bomb**: Injects an astronomically high bitrate I-frame burst. Verifies that the PI controller drops the bandwidth sharply to prevent exceeding the 95% buffer HWM without losing packets.
- **The Discontinuity Storm**: Bombards the input with `discontinuity_indicator == 1` flags to ensure the PLL state machine resets reliably without deadlocking.
- **The Silent Source**: Abrasively cuts the input stream for 5 seconds. Verifies the Pacer immediately synthesizes NULL packets (PID `0x1FFF`) to maintain the physical CBR and resynchronizes smoothly when the source returns.

## 8. Ecosystem Integration (FFmpeg)
To transform `libtsshaper` into a fully-fledged commercial encoder, it functions as an invisible proxy beneath FFmpeg.
By customizing the `AVIOContext` `write_packet` callback, FFmpeg acts solely as a syntax packer (VBR), while `libtsshaper` intercepts the 188-byte chunks. The integration applies semantic hints (e.g., Video vs. PSI/SI), executes T-STD mathematical modeling, and performs the physical CBR emission, bypassing FFmpeg's built-in, soft-real-time muxer completely.

## 10. Regression Testing & Verification

To ensure 1:1 functional parity with the native T-STD engine and maintain professional broadcasting SLAs, a comprehensive regression suite is provided.

- **Verification Guide**: Detailed KPIs and manual audit procedures are documented in [docs/VERIFICATION_GUIDE.md](./docs/VERIFICATION_GUIDE.md).

### 10.1 Automated Workflow

The regression process is split into two clear steps: Integration and Testing.

**Step 1: Build and Integrate**
Compile `libtsshaper` and automatically embed it into the custom FFmpeg builder:
```bash
chmod +x scripts/build_ffmpeg_integration.sh
./scripts/build_ffmpeg_integration.sh
```

**Step 2: Run the Regression Suite**
Execute the full FFmpeg-driven regression matrix against the newly compiled engine:
```bash
chmod +x scripts/tstd_regression_suite.sh
./scripts/tstd_regression_suite.sh --all
```

The suite validates:
- **Bitrate Smoothness**: Ensuring Delta < 88kbps and Score < 350.
- **PCR Precision**: Verifying nanosecond-level jitter compliance.
- **Timeline Resilience**: Testing 33-bit wrap-around and discontinuity handling.
- **Buffer Integrity**: Monitoring TB_n and VBV models for overflows.
