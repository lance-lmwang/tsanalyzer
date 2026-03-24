# FFmpeg MPEG-TS Muxer T-STD Scheduler Design

## 1. Executive Summary
This document outlines the architectural changes required in `libavformat/mpegtsenc.c` to support the **Transport System Target Decoder (T-STD)** model as defined in **ISO/IEC 13818-1**. The goal is to produce Constant Bitrate (CBR) Transport Streams with stable PCR intervals and compliant buffer behavior, suitable for broadcast and professional verified environments.

## 2. Problem Statement
The current FFmpeg MPEG-TS muxer uses a **synchronous, greedy** approach:
1.  **Blocking Writes**: When `mpegts_write_pes` is called (e.g., for a 200KB I-frame), it monopolizes the output, writing hundreds of TS packets in a burst.
2.  **PCR Jitter**: During this burst, no other stream (audio/PCR) can be interleaved. If the I-frame takes 100ms to transmit, the PCR interval will drift by at least 100ms, violating the strict limits (e.g., 40ms or 100ms).
3.  **Bitrate Violation**: The `muxrate` control relies on inserting NULL packets *after* a burst or statically between PES packets, leading to significant VBV violations and uneven output rates.

## 3. Architecture Proposal: The T-STD Scheduler

We propose moving from a **Push-Based** model to a **Pull-Based (Scheduler)** model.

### 3.1 Core Concept
Instead of writing directly to the output file (`pb`), `mpegts_write_pes` will packetize PES data into 188-byte TS packets and push them into **Per-Stream FIFO Queues**.

A new **Scheduler** function will be called repeatedly to:
1.  Check the **Global Time (STC)**.
2.  Check **PCR obligations**.
3.  Check **T-STD Buffer Models** (TB/MB/EB) for all streams.
4.  **Select** the optimal packet to send (Video, Audio, PSI/SI, or NULL).

### 3.2 Key Components

#### 3.2.1 Per-Stream Queue (`AVFifo`)
Each `MpegTSWriteStream` will maintain a FIFO queue of fully formed TS packets (188 bytes).
- **Input**: `mpegts_write_pes` fills this queue.
- **Output**: The Scheduler consumes from this queue.

#### 3.2.2 T-STD State Machine
We will simulate the T-STD buffers for scheduling decisions:
- **Transport Buffer (TB)**: Smoothed input rate ($R_{ts}$).
- **Multiplexing Buffer (MB)**: For video, smoothing elementary stream irregularities.
- **Elementary Buffer (EB)**: The decoder buffer (VBV).

#### 3.2.3 Scheduler Logic (The "Heartbeat")
The scheduler decides the content of the *next* transport packet slot.

**Priority Hierarchy:**
1.  **PCR Injection (Critical)**:
    - If `CurrentTime >= NextPCRTime`:
        - **Option A**: If a video TS packet is ready in the queue, inject PCR into its adaptation field (stealing payload space if necessary, or using existing adaptation).
        - **Option B**: If no video packet is ready, generate a **PCR-only** packet (adaptation field only).
2.  **PSI/SI (High)**: PAT, PMT, SDT sections that are due (based on repetition intervals).
3.  **Audio (Medium)**: To prevent audio buffer underflow (audio buffers are smaller).
4.  **Video (Low)**: Main payload.
5.  **NULL (Fallback)**: If no data is available or to maintain CBR padding.

### 4. Implementation Details

#### 4.1 Struct Changes (`MpegTSWriteStream`)
```c
typedef struct MpegTSWriteStream {
    // Existing fields...

    // New: T-STD Scheduling
    AVFifo *ts_packet_queue;  // Queue of 188-byte packets
    int64_t last_departure_time;

    // Buffer Simulation
    double tb_level;          // Transport Buffer Level
    double mb_level;          // Multiplexing Buffer Level
    double eb_level;          // Elementary Buffer Level

    // Constraints
    int64_t next_pcr_time;    // Next scheduled PCR insertion
} MpegTSWriteStream;
```

#### 4.2 Scheduler Loop (`mpegts_scheduler`)
```c
static int mpegts_scheduler(AVFormatContext *s) {
    MpegTSWrite *ts = s->priv_data;

    // 1. Update Virtual Clock (based on bytes written * muxrate)
    int64_t current_time = ts->bytes_written * 8 * 27000000 / ts->mux_rate;

    // 2. Check PCR
    if (current_time >= ts->next_pcr_time) {
        return write_pcr_packet(s); // Inject or separate
    }

    // 3. Select Stream (Weighted Fair Queuing or Earliest Deadline First)
    MpegTSWriteStream *best_st = NULL;
    // ... selection logic based on buffer fullness ...

    // 4. Write Packet or NULL
    if (best_st) {
        pop_and_write(best_st);
    } else {
        write_null_packet(s);
    }
}
```

## 5. References
- **ISO/IEC 13818-1**: Annex D (T-STD Model).
- **ETSI TR 101 290**: Measurement guidelines (Priority 1: PCR Accuracy).

## 6. Validation Strategy
1.  **PCR Jitter**: Must be < 500ns.
2.  **Buffer Analysis**: Use `tsanalyzer` to verify no underflows/overflows in the generated stream.
3.  **Bitrate**: Output must be strict CBR (within 1% tolerance).
