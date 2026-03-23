# FFmpeg Integration Guide: libtsshaper

## 1. Architectural Philosophy
To achieve professional broadcast-grade output, we utilize a **Hybrid Muxing Architecture**. FFmpeg handles the high-level syntax multiplexing (PSI/SI generation, PES encapsulation), while `libtsshaper` acts as the physical-layer gatekeeper, ensuring nanosecond-precision pacing and T-STD compliance.

## 2. Integrated Bitrate Strategy (Hybrid Mode)
Unlike standard VBR shaping, we recommend the following hybrid configuration to preserve FFmpeg's internal T-STD and PCR timing logic:

| Layer | Configuration | Purpose |
| :--- | :--- | :--- |
| **FFmpeg** | Set `muxrate` to 100% of target. | Ensures correct initial PCR/DTS/PTS relationship. |
| **libtsshaper** | Set target CBR to exact physical rate. | Smooths out FFmpeg's bursty software emission. |

**Why this works**: FFmpeg generates the "bulk" of the CBR stream (including NULL padding). `libtsshaper` intercepts this stream, smooths the physical inter-packet gaps, restamps PCRs to match the actual emission time, and adds trace amounts of padding if FFmpeg's software timer slightly underperforms.

## 3. High-Precision IO Bridge
Integration is performed by implementing a custom `AVIOContext` write callback. This bridge must enforce **Hard Backpressure** to synchronize FFmpeg's multiplexing speed with the network emission rate.

### 3.1 Implementation: `ffmpeg_to_shaper_write`
This function must be robust against unaligned writes and handle congestion via thread suspension.

```c
/**
 * @brief Bridge callback for FFmpeg's AVIOContext.
 */
static int ffmpeg_to_shaper_write(void *opaque, uint8_t *buf, int buf_size) {
    tsshaper_t *shaper = (tsshaper_t *)opaque;

    /* 1. Defensive Alignment Check */
    if (buf_size % 188 != 0) {
        // Log warning and align to TS packet boundary
        buf_size = buf_size - (buf_size % 188);
    }

    for (int i = 0; i < buf_size; i += 188) {
        uint8_t *pkt = buf + i;

        /* 2. Sync Byte Validation */
        if (pkt[0] != 0x47) continue;

        uint16_t pid = ((pkt[1] & 0x1F) << 8) | pkt[2];
        tss_pid_type_t type = TSS_PID_TYPE_VIDEO;

        /* 3. Deep Packet Inspection for Semantic Hinting */
        if (pid == 0x0000 || pid == 0x0001 || pid == 0x0011) {
            type = TSS_PID_TYPE_PSI_SI;
        } else if ((pkt[3] & 0x20) && pkt[4] > 0 && (pkt[5] & 0x10)) {
            // Adaptation Field exists AND PCR flag is set
            type = TSS_PID_TYPE_PCR;
        }

        /* 4. Hard Backpressure (Blocking Push) */
        // We MUST block FFmpeg if the shaper's ingest queue is full.
        // Failing to block will cause FFmpeg to trigger a fatal EIO.
        while (tsshaper_push(shaper, pid, type, pkt, 0) != 0) {
            // High-precision backoff to yield CPU during congestion
            struct timespec ts = {0, 10000}; // 10us
            nanosleep(&ts, NULL);
        }
    }
    return buf_size;
}
```

## 4. Operational Best Practices
1.  **Buffer Sizing**: Initialize `avio_alloc_context` with a buffer size that is a multiple of 188 (e.g., 32712 bytes) to minimize the overhead of the alignment checks.
2.  **Thread Affinity**: If possible, pin the FFmpeg muxing thread and the `libtsshaper` pacer thread to different physical cores to prevent cache thrashing during high-throughput (>1Gbps) operations.
3.  **PCR Restamping**: Ensure `libtsshaper` is configured to perform JIT PCR restamping to account for the latency introduced by the ingest queue and the backpressure mechanism.
