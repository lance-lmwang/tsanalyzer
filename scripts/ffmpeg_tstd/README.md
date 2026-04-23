# FFmpeg T-STD Test & Metrology Suite

## 1. Professional Entry Points
Use these scripts for different validation tiers:

| Command | Purpose | Expected Result |
| :--- | :--- | :--- |
| **`./tstd_full_validation.sh`** | **Final Release Audit.** Runs all tiers. | OVERALL STATUS: GOLDEN |
| **`./tstd_regression_suite.sh`** | **Comprehensive CI.** Edge case verification. | ALL REGRESSION PHASES PASSED |
| **`./tstd_master_audit.sh 1`** | **Bitrate Benchmarking.** Parallel matrix test. | SCORE < 150 for all typical rates. |

---

## 2. New: Compliance & Resilience Tools
Added in V7.5 for industrial-grade robustness verification:

*   **`./tstd_psi_audit.sh`**: **PSI Interval Compliance.** Automatically verifies PAT/PMT (<500ms) and SDT (<2000ms) using TSDuck. Crucial for Promax 3.4 error prevention.
*   **`./tstd_jump_audit.sh`**: **Timeline Jump Recovery.** Stress tests the V6 Voter system using an 8-hour timestamp gap sample.
*   **`./tstd_audio_only_audit.sh`**: **Audio-Only Resilience.** Verifies vSTC progression and SDT stability when no video stream is present.
*   **`./tstd_chaos_audit.sh`**: **Chaos Jitter Audit.** Simulates network packet loss and timestamp jitter to verify engine survival.

---

## 3. Regression Test SOP (Standard Operating Procedure)

To conduct a full regression after code changes in FFmpeg:

### Step 1: Environment Setup
Ensure you have the License Key path exported:
```bash
export WZ_LICENSE_KEY="/home/lmwang/dev/cae/wz_license.key"
```

### Step 2: Build FFmpeg
```bash
cd /home/lmwang/dev/cae/ffmpeg.wz.master
./scripts_ci/docker_build.sh
```

### Step 3: Run Full Audit
```bash
cd /home/lmwang/dev/cae/tsanalyzer
./scripts/ffmpeg_tstd/tstd_full_validation.sh
```

---

## 4. Auditor Reference (Core Logic)
*   **`tstd_audit_v2.py`**: PCR-timeline based interval auditor. Detects time-axis desync.
*   **`tstd_boundary_audit.py`**: Startup/Drain gap auditor. Detects trailing padding or early truncation.
*   **`ts_expert_auditor.py`**: Core physical sampling engine for bitrate precision.

**Status: Production Ready (V7.5 Gold)**
