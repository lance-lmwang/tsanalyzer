# FFmpeg T-STD Test & Metrology Suite

## 1. Professional Entry Points
Use these scripts for different validation tiers:

| Command | Purpose | Expected Result |
| :--- | :--- | :--- |
| **`./tstd_full_validation.sh`** | **Final Release Audit.** Runs all tiers. | OVERALL STATUS: GOLDEN |
| **`./tstd_regression_suite.sh`** | **Comprehensive CI.** Edge case verification. | ALL REGRESSION PHASES PASSED |
| **`./tstd_master_audit.sh 1`** | **Bitrate Benchmarking.** Parallel matrix test. | SCORE < 150 for all typical rates. |

---

## 2. Regression Test How-To (Standard Operating Procedure)

To conduct a full regression after code changes in FFmpeg:

### Step 1: Environment Setup
Ensure you have the License Key path exported:
```bash
export WZ_LICENSE_KEY="/home/lmwang/dev/cae/wz_license.key"
```

### Step 2: Build FFmpeg
Always run the CI build script first to ensure binary consistency:
```bash
cd /home/lmwang/dev/cae/ffmpeg.wz.master
./scripts_ci/docker_build.sh
```

### Step 3: Run Full Audit
```bash
cd /home/lmwang/dev/cae/tsanalyzer
./scripts/ffmpeg_tstd/tstd_full_validation.sh
```

### Step 4: Verification Criteria
1.  **Physical Latency**: Check `Actual XXXms`. Must be close to configured `muxdelay`.
2.  **Duration**: Check `Actual XXs (Expected XXs)`. Gap must be < 1.5s.
3.  **Stability**: No `DRIVE FUSE` or `Panic Recovery` logs in the output.

---

## 3. Tool Reference
*   **`tstd_telemetry_analyzer.py`**: Parsers logs into JSON reports.
*   **`ts_expert_auditor.py`**: Core physical sampling engine.

**Status: Production Ready**
