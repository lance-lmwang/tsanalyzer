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

### Daily Regression Recommendations
To ensure the T-STD engine remains bit-accurate and compliant after any codebase modifications, **always run the UDP End-to-End CBR test** as your primary gatekeeper.

1. **Primary Gate (UDP Real-time Metrology)**:
   ```bash
   ./scripts/ffmpeg_tstd/ffmpeg_tstd_udp_cbr.sh
   ```
   *Pass Criteria*: `Score: 100/100` and `[PASS] ES Layer and Timestamps verified.`

2. **Long-Term Soak Test**:
   ```bash
   ./scripts/ffmpeg_tstd/ffmpeg_tstd_run_stable.sh
   ```
   *Pass Criteria*: `PCR_drift_ppm: 0.000` after extended runs.

3. **Chaos/Discontinuity Re-encode Test**:
   ```bash
   ./scripts/ffmpeg_tstd/ffmpeg_tstd_jaco_reencode.sh
   ```
   *Pass Criteria*: Engine must survive input timestamp jumps/rollbacks and output a smooth CBR stream.

---

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

## 📈 Bitrate Precision Audit

To meet broadcast-grade requirements where bitrate fluctuation must be within 64kbps (1s window), the suite provides two measurement methodologies:

### 1. Internal Telemetry Audit (Log-based)
**Tool**: `tstd_bitrate_auditor.py`
**Description**: Analyzes FFmpeg internal `[T-STD]` telemetry logs. It represents the theoretical precision at the **scheduler exit point**.
**Usage**:
```bash
# Audit Video PID 0x0100 (default), 1s window, skip first 3s
./scripts/ffmpeg_tstd/tstd_bitrate_auditor.py --log output/tstd_smoke.log --pid 0x0100 --window 1.0 --skip 3.0 --verbose
```

### 2. Physical TS Verification (Bitstream-based)
**Tool**: `ts_pid_bitrate_pcr_analyzer.py`
**Description**: **Log-independent**. Directly parses the `.ts` file and established a high-precision timeline via PCR (Program Clock Reference). It reflects the **actual physical behavior** seen by a receiver.
**Usage**:
```bash
# Verify physical video bitrate in the TS file (skip first 5s)
./scripts/ffmpeg_tstd/ts_pid_bitrate_pcr_analyzer.py output/tstd_smoke.ts --pid 0x0100 --skip 5.0
```

---

## 📁 Key Scripts Summary

| Script | Purpose |
| :--- | :--- |
| `ffmpeg_tstd_smoke_test.sh` | Core 30s regression test. Triggers full compliance audit. |
| `ffmpeg_tstd_verify_compliance.sh` | The automated auditor. Enforces hard gates (PCR, Jitter, 64k Bitrate). |
| `ffmpeg_tstd_mode_comparison.sh` | Side-by-side comparison of Mode 1 (Strict) vs Mode 2 (Elastic). |
| `tstd_bitrate_auditor.py` | Professional tool for log-based bitrate stability analysis. |
| `ts_pid_bitrate_pcr_analyzer.py` | Independent physical layer bitrate analyzer using PCR timing. |
| `ts_bitrate_pcr_analyzer.py` | Calculates the total physical muxrate (all PIDs) between PCRs. |
| `ffmpeg_tstd_run_stable.sh` | Long-term soak test for the T-STD engine. |
| `ffmpeg_tstd_udp_cbr.sh` | Real-time UDP streaming metrology test. |
| `ffmpeg_tstd_jaco_reencode.sh` | Validation against specifically anomalous broadcast samples. |

---

## 📊 Result Interpretation

*   **Score >= 80**: **PASS**. The stream is safe for professional IRDs.
*   **Video/Audio Underflow > 0**: **CRITICAL FAIL**. Decoders will experience freeze or macroblocks.
*   **PCR Error**: Measures the distance between Muxer clock and recovered STC.
*   **Jitter**: Measures the stability of the output clock stream.
*   **Bitrate Fluctuation**: Must be < 64kbps for 1s window to meet carrier-grade CBR requirements.
