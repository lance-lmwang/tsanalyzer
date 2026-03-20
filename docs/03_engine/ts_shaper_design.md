# Engineering Design: libtsshaper (Professional TS Shaper & T-STD Engine)

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
       │ Layer 2: Interleaver (Urgency-based Arbiter)     │
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

### 3.1 Discrete-Event T-STD Simulation
To eliminate cumulative drift inherent in continuous models, buffer levels ($B$) are calculated only at discrete packet arrival/departure points.

**State Update at $t_{i}$ (Packet Arrival):**
1.  **Leak Phase**: $B(t_{i}^{-}) = \max(0, B(t_{i-1}^{+}) - R_{leak} \cdot (t_{i} - t_{i-1}))$
2.  **Fill Phase**: $B(t_{i}^{+}) = B(t_{i}^{-}) + Size_{packet}$

**Constraints & Safety Margin:**
- $B(t) \le B_{max}$: Violations trigger **Immediate Backpressure**.
- **Proactive Throttle**: A **High-Water Mark (HWM) at 95% of $B_{max}$** is used to signal the StatMux to aggressively reduce the target bitrate ($R_{i}$) before a hard violation occurs.
- $B(t) \ge 0$: Violations trigger **Underflow Protection** (Insert Padding/NULLs to prevent starvation).

### 3.2 VBV-Aware StatMux (Feedback Loop)
Bandwidth allocation $R_{i}$ uses a Proportional-Integral (PI) controller:
$$R_{i}(t) = K_{p} \cdot C_{i} + K_{i} \cdot (F_{target} - F_{current})$$
- **K_p (Proportional)**: Reacts to immediate complexity spikes (e.g., scene cuts).
- **K_i (Integral)**: Corrects long-term drift and maintains the HWM safety margin.

### 3.3 PCR Regulator & PLL
PCR is a **Timing Function** anchored to the physical emission clock:
- **Interval Regulation**: Forces PCR insertion if no anchor is seen for $> 35ms$.
- **Precision Rewrite**: PCR values are calculated using the *Target Emission Time* ($T_{emit}$) at the nanosecond level.
- **PLL Smoothing**: A software PLL filters upstream PCR jitter, ensuring downstream clock recovery is stable and low-drift.

## 4. Engineering Implementation & Real-Time HAL

### 4.1 High-Precision Pacer Loop
1.  **Stage 1 ($> 1ms$)**: `nanosleep()` or `poll()` to yield CPU and save power.
2.  **Stage 2 ($10\mu s - 1ms$)**: `sched_yield()` to minimize context switch latency.
3.  **Stage 3 ($< 10\mu s$)**: **Hot-spin** using the `PAUSE` instruction (`cpu_relax()`). This prevents CPU pipeline stalls and ensures zero-latency transition to the `sendmmsg` syscall at $T_{now} \ge T_{emit}$.

### 4.2 Cache & SIMD Optimization
- **False Sharing**: Producer and Consumer indices are padded to 128 bytes to prevent cache line contention.
- **SIMD Alignment**: All 188-byte TS payloads are aligned to **32-byte boundaries**. This enables future-proof AVX-512 based packet processing or scrambling with zero alignment overhead.
- **Metadata Locality**: Metadata is stored in a separate hot-cache array to maximize prefetch efficiency during scheduling.

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
 * Push a packet with an external arrival timestamp.
 * If arrival_ts is 0, the library uses internal CLOCK_MONOTONIC_RAW.
 * @param pid The PID of the packet.
 * @param pkt Pointer to the 188-byte TS packet.
 * @param arrival_ts External timestamp in nanoseconds.
 * @return 0 on success, -1 if the ingest buffer is full.
 */
int tsshaper_push(tsshaper_t* ctx,
                  uint16_t pid,
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
