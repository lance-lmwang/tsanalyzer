# Engineering Design: LibTSMux (High-Precision T-STD Spacer)

## 1. Executive Summary
This document specifies the architecture for `libtspacer`, a high-performance, thread-safe C library designed to enforce Constant Bitrate (CBR) and precise PCR timing for MPEG-TS streams. It acts as a shaping layer between a muxer (e.g., FFmpeg) and the output sink (UDP/File), ensuring compliance with ETSI TR 101 290.

## 2. Design Goals
1.  **Strict CBR**: Output bitrate matches target within < 1% tolerance.
2.  **PCR Precision**: Jitter < 500ns via "Just-in-Time" PCR rewriting.
3.  **Low Latency**: Lock-free ring buffer architecture.
4.  **Integration**: ABI-stable C interface for easy embedding in FFmpeg/GStreamer.

## 3. Public API Specification (`tsa_spacer.h`)

The API uses an opaque handle pattern (`tsa_spacer_t`) to maintain ABI stability.

```c
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque context handle
typedef struct tsa_spacer tsa_spacer_t;

// TS Packet Metadata (Zero-Copy friendly)
typedef struct {
    uint8_t* data;       // Pointer to 188-byte packet
    int64_t  pts;        // Optional: PTS for analysis
    int64_t  dts;        // Optional: DTS for scheduling
    int      has_pcr;    // Flag: 1 if this packet contains a PCR field to be rewritten
} tsa_ts_packet_t;

/**
 * Create a new spacer instance.
 * @param bitrate_bps Target bitrate in bits per second (CBR).
 * @param fd File descriptor to write to (socket or file).
 * @return Handle to spacer, or NULL on failure.
 */
tsa_spacer_t* tsa_spacer_create(uint64_t bitrate_bps, int fd);

/**
 * Destroy the spacer instance.
 * Stops the worker thread and frees resources.
 */
void tsa_spacer_destroy(tsa_spacer_t* ctx);

/**
 * Start the pacing worker thread.
 * @return 0 on success, < 0 on error.
 */
int tsa_spacer_start(tsa_spacer_t* ctx);

/**
 * Stop the pacing worker thread.
 */
void tsa_spacer_stop(tsa_spacer_t* ctx);

/**
 * Push a packet into the queue (Non-blocking).
 * @param ctx Spacer handle.
 * @param pkt Packet metadata (data is copied into internal ring buffer).
 * @return 0 on success, -1 if queue is full.
 */
int tsa_spacer_push(tsa_spacer_t* ctx, const tsa_ts_packet_t* pkt);

/**
 * Helper: Push a raw 188-byte packet.
 * Automatically detects PCR presence (adaptation_field_control).
 */
int tsa_spacer_push_raw(tsa_spacer_t* ctx, const uint8_t* data);

/**
 * Update target bitrate dynamically.
 */
void tsa_spacer_set_bitrate(tsa_spacer_t* ctx, uint64_t bitrate_bps);

// Statistics
typedef struct {
    uint64_t bytes_sent;      // Total bytes written to FD
    uint64_t null_inserted;   // Total NULL packets generated
    int64_t  last_pcr;        // Last written PCR value (90kHz base + ext)
    double   pcr_jitter_ns;   // Estimated jitter
    uint32_t queue_depth;     // Current packets in queue
    uint32_t queue_overflows; // Dropped packets due to full queue
} tsa_spacer_stats_t;

void tsa_spacer_get_stats(tsa_spacer_t* ctx, tsa_spacer_stats_t* stats);

#ifdef __cplusplus
}
#endif
```

## 4. Internal Architecture

### 4.1 Data Structures

**Ring Buffer (`tsa_ringbuf`)**
A fixed-size, lock-free SPSC (Single Producer Single Consumer) queue using `std::atomic`.
*   **Size**: $2^{16}$ packets (approx 12MB), sufficient for ~1 sec of 100Mbps video.
*   **Node**:
    ```cpp
    struct Node {
        uint8_t data[188];
        bool    has_pcr;
    };
    ```

**Context (`tsa_spacer`)**
```cpp
struct tsa_spacer {
    int fd;
    uint64_t bitrate;
    std::atomic<bool> running;
    std::thread worker;

    // Ring Buffer state
    std::vector<Node> queue;
    std::atomic<size_t> head; // Consumer index
    std::atomic<size_t> tail; // Producer index
    size_t mask;              // size - 1

    // Pacing state
    uint64_t bytes_sent;
    int64_t  start_time_ns;

    // Stats
    std::atomic<uint64_t> null_count;
};
```

### 4.2 The Pacing Loop (Critical Path)

The worker thread executes the following logic in a tight loop:

1.  **Virtual Clock Sync**:
    Calculate the *Target Emission Time* ($T_{target}$) based on `bytes_sent` and `bitrate`.
    $$T_{target} = T_{start} + \frac{BytesSent \times 8}{Bitrate} \times 10^9$$

2.  **Busy-Wait Precision**:
    Compare $T_{target}$ with Current Time ($T_{now}$).
    *   If $T_{now} < T_{target}$, execute `cpu_relax()` (PAUSE instruction).
    *   This ensures emission timing is accurate to microsecond levels.

3.  **Data Fetch**:
    Check the Ring Buffer.
    *   **Data Available**: Pop packet $P$.
    *   **Empty**: Generate a NULL packet ($P_{null}$) to maintain CBR.

4.  **PCR Rewrite**:
    If $P$ has `has_pcr` flag set:
    *   Calculate correct PCR from $T_{target}$.
    *   Base ($33$ bits) = $T_{target} \times 90000 / 10^9$.
    *   Ext ($9$ bits) = $(T_{target} \times 27000000 / 10^9) \pmod{300}$.
    *   Modify bytes 6-11 of the packet in-place.

5.  **IO**:
    `write(fd, P.data, 188)`.
    Increment `bytes_sent`.

## 5. Thread Safety & Performance
*   **Lock-Free Ingest**: `tsa_spacer_push` uses atomic loads/stores with `memory_order_acquire`/`release` to ensure thread safety without mutex contention.
*   **Zero Allocation**: All memory is pre-allocated at `create`. No `malloc` in the hot path.
*   **Cache Friendly**: Ring buffer nodes are aligned to cache lines where possible.

## 6. FFmpeg Integration Strategy
*   **Location**: `libavformat/mpegtsenc.c`
*   **Hook**: Inside `mpegts_write_pes` and `mpegts_write_section`.
*   **Logic**:
    1.  Instead of `avio_write`, packetize data into 188-byte chunks.
    2.  Call `tsa_spacer_push_raw(spacer, chunk)`.
    3.  FFmpeg manages buffering for "interleaving", but the Spacer manages "timing".

## 7. Future Extensions
*   **T-STD Compliance**: Add logic to track TB/MB/EB buffer levels and pause/drop packets if they would cause overflow (Backpressure).
*   **SRT Integration**: Use `libsrt`'s non-blocking send directly in the spacer for low-latency streaming.
