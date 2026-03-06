# Implementation Plan: World-Class Metrology Engine

This plan breaks down the `spec.md` into actionable, incremental steps.

## Step 1: Metrology Core & PCR Linear Trend (Walltime Drift) (DONE)
- **Goal**: Implement the mathematical core for PCR clock drift analysis using linear regression.
- **Tasks**:
  - Create `src/metrology/` directory.
  - Implement `src/metrology/pcr_trend.c` and `pcr_trend.h`. This will use a rolling window of (SystemTime, PCRTime) tuples to compute the slope (drift) and intercept.
  - Integrate `pcr_trend` into the existing `tsa_engine_pcr.c`.
  - Export `pcr_walltime_drift_ms` and `pcr_drift_ppm` in the snapshot structure.
- **Validation**: Replay a stable TS file and ensure drift is near 0. Replay a modified TS file with scaled PCRs and observe the drift rate accurately reflecting the scaling.

## Step 2: IAT (Inter-Arrival Time) Histograms (DONE)
- **Goal**: Measure and visualize network micro-bursts.
- **Tasks**:
  - Implement `src/metrology/iat_histogram.c`.
  - Maintain an array of "buckets" (e.g., `<1ms`, `1-2ms`, `2-5ms`, `5-10ms`, `>10ms`).
  - Upon every packet reception in `tsa_stream.c` or the ingestion layer, calculate $\Delta T$ from the previous packet and increment the appropriate bucket.
  - Export these histogram buckets to the shared memory and Prometheus JSON.
- **Validation**: Use `tc qdisc` (netem) to inject artificial network delay bursts and verify the histogram reflects the injected jitter.

## Step 3: NALU Sparse Sniffer & PES Deep Inspection (DONE)
- **Goal**: Detect video frame types (I/P/B) and GOP boundaries without decoding the video.
- **Tasks**:
  - Implement `src/metrology/nalu_sniffer.c`.
  - When `tsa_engine_essence.c` detects a PES payload starting with `0x00 00 01`, parse the PES header to find the start of the H.264/H.265 payload.
  - Scan for NALU headers and identify Slice Types (IDR/I/P/B).
  - Track GOP sizes (number of frames between I-frames) and report them.
- **Validation**: Analyze a known H.264 stream and verify the reported GOP length and I-frame intervals match `ffprobe` output.

## Step 4: High-Precision SCTE-35 Audit
- **Goal**: Calculate the precise error between Ad-Insertion markers and video I-frames.
- **Tasks**:
  - Enhance `tsa_engine_scte35.c` to store the latest `pts_time` associated with an upcoming splice event.
  - Connect this with the `nalu_sniffer` from Step 3.
  - When the sniffer detects the next IDR/I-Frame, calculate the difference between its PTS and the stored SCTE-35 `pts_time`.
  - Report `scte35_pts_alignment_offset_ms`.
- **Validation**: Inject SCTE-35 markers into a test stream and verify the offset is calculated.

## Step 5: Forensic Triggered Micro-Capture
- **Goal**: Automatically save the "crime scene" when critical errors occur.
- **Tasks**:
  - Implement a global lock-free ring buffer `tsa_micro_capture.c` (e.g., allocating ~5MB to hold ~500ms of TS packets).
  - Every incoming TS packet is written to this buffer continuously.
  - Modify `tsa_engine_tr101290.c` to emit a "Critical Trigger" event on P1 errors (like Sync Loss or CC Error).
  - Upon trigger, freeze the capture buffer, spawn a background thread (or asynchronous task) to flush it to `/tmp/tsa_capture_<timestamp>.ts`, and resume.
- **Validation**: Use a chaos script to drop packets. Verify that a `.ts` file is generated and contains the exact packet drop moment.

## Step 6: Bitrate Smoother (Optional/Stretch)
- **Goal**: De-jittering capability.
- **Tasks**:
  - Build an output module `tsa_smoother.c` that consumes the internal ring buffer and paces UDP transmission perfectly spaced by the calculated `pcr_expected` time.
