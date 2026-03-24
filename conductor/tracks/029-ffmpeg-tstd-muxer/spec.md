# Track 029: FFmpeg MPEG-TS Muxer T-STD Support

## Objective
Implement T-STD (Transport System Target Decoder) model support in FFmpeg's MPEG-TS muxer to ensure constant bitrate (CBR) compliance, stable PCR intervals, and compliant buffer behavior.

## Problem Statement
The current FFmpeg `mpegtsenc.c` implementation, even with `muxrate` set, exhibits:
1.  **High ES Bitrate Variance**: Due to "Just-in-Time" padding logic and lack of look-ahead smoothing.
2.  **Unstable PCR Intervals**: Large video frames (I-frames) block the muxer loop, delaying PCR insertion and preventing audio packet interleaving.
3.  **T-STD Violation**: No explicit model checks for TB, MB, or EB buffer levels, risking underflow/overflow on strict receivers.

## References
1.  **ETSI TR 101 290**: Measurement guidelines for DVB systems.
2.  **ISO/IEC 13818-1**: Generic coding of moving pictures and associated audio information: Systems (T-STD model definition).
3.  **User Report**: "ES bitrate fluctuation is too large, and PCR interval is unstable."

## Goals
1.  **Stable CBR**: Achieve flat ES bitrate output when configured.
2.  **PCR Precision**: Maintain PCR jitter within standard limits (e.g., < 500ns or as per profile).
3.  **Buffer Compliance**: Ensure output stream respects T-STD buffer models.

## Strategy
1.  **Baseline Analysis**: Generate a test stream with current FFmpeg and analyze it using `tsanalyzer` to quantify the defects.
2.  **Architecture Redesign**:
    *   Move from "blocking write" to "queued write" architecture in `mpegtsenc.c`.
    *   Implement a **T-STD Scheduler** that selects packets from per-stream queues based on buffer status and target bitrate.
3.  **Implementation**: Modify `libavformat/mpegtsenc.c`.
4.  **Validation**: Verify with `tsanalyzer` against the baseline.
