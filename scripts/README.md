# TsAnalyzer Scripts Archive System

## 📂 Directory Structure Overview

| Directory | Category | Description | Key Entrypoint |
| :--- | :--- | :--- | :--- |
| `ffmpeg_tstd/` | **FFmpeg T-STD Core** | Specialized T-STD Muxer validation. | `ffmpeg_tstd_run_stable.sh` |
| `verification/` | **Atomic Verification** | Small-scale feature validation (CBR, PCR, CC). | `verify_tstd_compliance.sh` |
| `e2e/` | **Scenario Tests** | Full-chain E2E integration scenarios. | `test-e2e.sh` |
| `stability/` | **Stability & Stress** | Long-run soak tests and extreme load. | `test_stability_5min.py` |
| `ops/` | **Operations & Env** | Deployment, initialization, and tuning. | `env_ready.sh` |
| `tools/` | **Power Tools** | Offline data analysis and generator utilities. | `pcr_analyzer.py` |
| `chaos/` | **Resilience** | Fault injection and chaos simulation. | `tsa_run_chaos_suite.sh` |
| `monitoring/` | **Dashboards & NOC** | Live UI and metric monitoring tools. | `tsa_monitor.py` |

---

## 🚀 Top 3 Most Important Scripts

1.  **`ffmpeg_tstd/ffmpeg_tstd_run_stable.sh`**: Runs the full T-STD muxer stability soak test.
2.  **`verification/verify_tstd_compliance.sh`**: The "referee" for T-STD conformance.
3.  **`stability/test_stability_5min.py`**: Standard 5-minute stability benchmark.

---

## 🛠 Usage Guardrails

*   **Pathing**: Most scripts use `SCRIPT_DIR=$(dirname $(readlink -f $0))` to handle relative paths correctly within subdirectories.
*   **Renaming**: T-STD scripts follow the `ffmpeg_tstd_` prefix standard.
*   **Environment**: Always run `ops/env_ready.sh` before starting a fresh validation cycle.
