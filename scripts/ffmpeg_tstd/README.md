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

The Architecture is designed to survive the following "killer" scenarios:

1. **PCR Jump (+8h)**: Instant 8-hour jump on PCR PID. Tests global offset absorption.
2. **Video Leads Jump**: Video DTS jumps +8h while PCR/Audio remain old. Tests Any-PID trigger logic.
3. **Audio Lag (-300s)**: Audio DTS is artificially delayed. Tests **Late Data Clamping** to prevent IRD underflow.
4. **PCR Jitter (±200ms)**: Random noise on PCR. Tests PLL stability and scheduling resilience.
5. **Saturated Muxrate**: High-load scenario (e.g., 1230k payload in 1300k pipe). Tests **Deadline-based Preemption**.

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

3. **Smoke Test Matrix**:
   ```bash
   ./scripts/ffmpeg_tstd/ffmpeg_tstd_smoke_test.sh
   ```
   *Description*: Runs a suite of tests including high-pressure, standard profile, and high-bitrate stress tests with automated bitrate fluctuation auditing.

---

## 📈 Bitrate Precision Audit

To meet broadcast-grade requirements where bitrate fluctuation must be within 64kbps (1s window), the suite provides two measurement methodologies:

### 1. Internal Telemetry Audit (Log-based)
**Tool**: `tstd_bitrate_auditor.py`
**Description**: Analyzes FFmpeg internal `[T-STD]` telemetry logs. It represents the theoretical precision at the **scheduler exit point**. Now refactored to use **absolute STC** for perfect timing accuracy even with sparse logging.
**Usage**:
```bash
# Audit Video PID 0x0100 (default), 1s window, skip first 3s and tail 3s
./scripts/ffmpeg_tstd/tstd_bitrate_auditor.py --log output/tstd_smoke.log --pid 0x0100 --window 1.0 --skip 3.0 --skip-tail 3.0
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

### Core Tests & Regression
| Script | Purpose |
| :--- | :--- |
| `ffmpeg_tstd_smoke_test.sh` | Core regression test. Triggers full compliance audit and bitrate matrix. |
| `ffmpeg_tstd_regression_production.sh` | Full 240s production regression suite with physical TS audit. |
| `ffmpeg_tstd_verify_compliance.sh` | The automated auditor. Enforces hard gates (PCR, Jitter, 64k Bitrate). |
| `ffmpeg_tstd_udp_cbr.sh` | Real-time UDP streaming metrology test. |
| `ffmpeg_tstd_run_stable.sh` | Long-term soak test for the T-STD engine. |
| `ffmpeg_tstd_chaos_test.sh` | Stress tests the engine with random timing discontinuities. |

### Diagnostic Tools
| Script | Purpose |
| :--- | :--- |
| `ffmpeg_tstd_physical_truth_check.sh` | Diagnostic tool to compare Log-based vs. Bitstream-based auditing. |
| `ffmpeg_tstd_log_redundancy_audit.sh` | Diagnoses 'Drive' log redundancy and multiplexer "spinning" issues. |
| `ffmpeg_tstd_auto_audit.sh` | Industrial auto-audit tool for reproducing skews and detecting drops. |
| `analyze_source_skew.py` | Tool to analyze and visualize clock skew in source streams. |
| `tstd_bitrate_auditor.py` | Professional tool for log-based bitrate stability analysis (STC-based). |
| `ts_pid_bitrate_pcr_analyzer.py` | Independent physical layer bitrate analyzer using PCR timing. |

### Comparison & Special Scenarios
| Script | Purpose |
| :--- | :--- |
| `ffmpeg_tstd_mode_comparison.sh` | Side-by-side comparison of Mode 1 (Strict) vs Mode 2 (Elastic). |
| `ffmpeg_tstd_saturated_muxrate_test.sh` | **Critical Stress Test**. Validates behavior when payload is near 100% of muxrate. |
| `ffmpeg_tstd_muxer_comparison.sh` | Benchmarks T-STD against other multiplexer implementations. |
| `ffmpeg_tstd_live_stress_test.sh` | Sustained stress test for live streaming scenarios. |
| `ffmpeg_tstd_sweep_test.sh` | Sweeps across multiple bitrates to find the stable operating boundary. |

---

## 📊 Result Interpretation

*   **Score >= 80**: **PASS**. The stream is safe for professional IRDs.
*   **Video/Audio Underflow > 0**: **CRITICAL FAIL**. Decoders will experience freeze or macroblocks.
*   **PCR Error**: Measures the distance between Muxer clock and recovered STC.
*   **Jitter**: Measures the stability of the output clock stream.
*   **Bitrate Fluctuation**: Must be within safety limits (e.g., 5% or 64kbps) to meet carrier-grade requirements.
