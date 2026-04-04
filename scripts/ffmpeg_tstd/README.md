# FFmpeg T-STD Industrial Validation Suite

This directory contains the specialized test suite for validating the **High-Precision T-STD Multiplexer** against real-world broadcast anomalies (PCR jumps, A/V interleaving drifts, and huge discontinuities).

## 🛠 Core Toolchain

| Tool | Location | Description |
| :--- | :--- | :--- |
| **Baseline Generator** | `ffmpeg` | Generates a clean, compliant TS stream as a test anchor. |
| **TS Mutator** | `../tools/ts_mutator.py` | Bit-accurate tool to inject timing disasters (8h jumps, jitter, lag). |
| **IRD Auditor** | `../tools/tstd_ird_compliance_audit.py` | The "Referee". Simulates a hardware decoder (PLL + T-STD Buffer). |

---

## 🧪 The Disaster Matrix (Test Scenarios)

The V4/V5 architecture is designed to survive the following "killer" scenarios:

1. **PCR Jump (+8h)**: Instant 8-hour jump on PCR PID. Tests global offset absorption.
2. **Video Leads Jump**: Video DTS jumps +8h while PCR/Audio remain old. Tests Any-PID trigger logic.
3. **Audio Lag (-300s)**: Audio DTS is artificially delayed. Tests **Late Data Clamping** to prevent IRD underflow.
4. **PCR Jitter (±200ms)**: Random noise on PCR. Tests PLL stability and scheduling resilience.

---

## 🚀 Standard Verification Workflow

### Step 1: Generate Baseline
```bash
ffmpeg -f lavfi -i testsrc=size=1280x720:rate=25 -f lavfi -i sine=frequency=1000 \
       -c:v libx264 -muxrate 4M -t 10 base.ts
```

### Step 2: Inject Anomalies
```bash
# Create a video-first 8-hour jump scenario
python3 ../tools/ts_mutator.py base.ts mut_video_jump.ts video_jump
```

### Step 3: Run Compliance Audit
```bash
# Single file audit with buffer visualization
python3 ../tools/tstd_ird_compliance_audit.py mut_video_jump.ts --plot

# Batch audit with HTML Dashboard generation
python3 ../tools/tstd_ird_compliance_audit.py *.ts
```

---

## 📊 Result Interpretation

*   **Score >= 80**: **PASS**. The stream is safe for professional IRDs.
*   **Video/Audio Underflow > 0**: **CRITICAL FAIL**. Decoders will experience freeze or macroblocks.
*   **PCR Error**: Measures the distance between Muxer clock and recovered STC.
*   **Jitter**: Measures the stability of the output clock stream.

## 📁 Key Scripts in this Folder

- `ffmpeg_tstd_run_stable.sh`: Long-term soak test for the T-STD engine.
- `ffmpeg_tstd_smoke_test.sh`: Quick 30s check for basic CBR pacing.
- `ffmpeg_tstd_udp_cbr.sh`: Real-time UDP streaming metrology test.
- `ffmpeg_tstd_jaco_reencode.sh`: Debug script for the anomalous Jaco broadcast sample.
