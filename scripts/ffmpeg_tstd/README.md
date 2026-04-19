# FFmpeg T-STD Test & Metrology Suite

## 1. Primary Validation
| Script | Purpose |
| :--- | :--- |
| **`tstd_full_validation.sh`** | **Master Entry.** Executes the complete pipeline: CI Regression, Matrix Audit, and Physical Truth Check. |
| **`tstd_regression_suite.sh`** | **Full Regression.** Covers all edge cases including 33-bit Rollover, Starvation, Chaos Jitter, Non-zero start (-copyts), and Real-time simulation. |
| **`tstd_master_audit.sh`** | **Performance Benchmarking.** Runs high-precision bitrate matrix audits (600k-1300k). |

## 2. Targeted Diagnostic Tools
*   **`tstd_overnight_stress.sh`**: Long-term (8h+) stability verification using real-time UDP streams and TSDuck monitoring.
*   **`tstd_physical_audit.sh`**: Bit-accurate consistency check between internal telemetry and physical TS files.
*   **`tstd_wrap_test.sh`**: Targeted verification for 33-bit DTS rollover.
*   **`tstd_starvation_recovery.sh`**: Targeted validation for engine self-healing after input stalls.

## 3. Analysis Engines
*   **`tstd_telemetry_analyzer.py`**: Converts raw logs into structured JSON health reports.
*   **`ts_expert_auditor.py`**: Broadcast-grade physical bitstream sampling and scoring.

---
**Status: Production Ready**
