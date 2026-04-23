# T-STD Physical Layer Verification & Review Guide (SOP)

> **FOR FUTURE AI REVIEWERS:** This document is the "Ground Truth" for the T-STD Pacing logic. Do not propose architectural changes to `tstd.c` without following this rigorous verification process.

## 1. The "Why" Behind the Logic
Current `v6.0` logic is optimized for **Physical Smoothness (Laminar Flow)**. It solves the three-way conflict between:
*   **Source VBR Jitter**: 1080i video has a natural variance (StdDev ~12%).
*   **Bitrate Delta Goal**: Professional analyzers (Physical Analyzer/Tektronix) require < 88kbps deviation.
*   **Compliance**: Strict adherence to ISO 13818-1 (TB_n=512) and TR 101 290.

## 2. Standard Verification Procedure (MUST FOLLOW)

If you intend to modify any constant (Gain, Bucket, Jitter Limit, Deadband):

### Step 1: Environment Setup
```bash
./scripts_ci/docker_build.sh
```

### Step 2: Running the Full Matrix Test
You must test across SD, 720p, and 1080i to ensure no regressions.
```bash
./tstd_shapability_matrix.sh all
```

### Step 3: Model Analysis (The "Physical Audit")
For each resolution, run the analyzer to check the **Safety Margin**.
```bash
python3 tsa_shapability_analyzer.py output/tstd_1080i_md0.9.v2.log 2300 0.9
```

## 3. How to Interpret Results (Reviewer Checklist)

### KPI A: Bitrate Delta (The "Smoothness" Test)
*   **Pass**: Delta <= 88k (for 1080i/1500k) or <= 64k (for SD/600k).
*   **Fail**: Any Delta > 120k in steady-state (10s+).
*   *If your change increases Delta, it is a regression.*

### KPI B: Video Delay (The "Timing" Test)
*   **Pass**: Max Video Delay should be within +/- 5% of the `-muxdelay` setting.
*   **Fail**: Delay > 1.6x target (triggers Panic mode) or < 0.1x target (starvation).

### KPI C: V-Wait(T) (The "Clustering" Test)
*   Check the log for `V-Wait(D:0, T:XX)`.
*   **Pass**: XX < 30 (indicates high granularity).
*   **Fail**: XX > 100 (indicates packet clustering/micro-bursts).

## 4. Why specific values are "Locked"
*   **10ms-20ms Bucket**: Essential for pulse shaping. Any larger causes "Clustering Jitter".
*   **+/- 32kbps Envelope**: Mathematically required to hit the < 88k Delta goal.
*   **15%-20% Deadband**: Matches the 12.2% StdDev of 1080i VBR noise. Reducing this causes the controller to "chatter".
*   **2000-sample Damping**: Prevents the Pace from reacting to "physiological" VBR fluctuations.

## 5. Summary
Any new AI reviewer must present a table comparing the **Old Delta vs New Delta** and **Old Delay vs New Delay** before asking for a merge. If the math doesn't show a clear physical improvement, **DO NOT CHANGE THE CODE.**
