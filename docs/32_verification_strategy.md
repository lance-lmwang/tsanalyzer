# TsAnalyzer: Professional Validation & Certification Strategy

This document defines the rigorous testing protocols required to certify TsAnalyzer as a "Broadcast-Grade" instrument. Verification is split into three core domains: Accuracy, Performance, and Stability.

---

## 1. Domain 1: Accuracy Proof (Metrology Validation)
*Goal: Ensure analysis results align with professional hardware (e.g., Tektronix, Sencore).*

### 1.1 PCR Jitter Benchmarking
- **Method**: Feed the same PCAP file into both TsAnalyzer and a reference hardware analyzer.
- **Test Cases**:
    - **Ideal Stream**: 0ns jitter baseline.
    - **Induced Jitter**: $\pm 500$ ns and $\pm 1000$ ns sine-wave jitter.
    - **Frequency Drift**: 1ms/s (approx 37ppm) linear drift.
- **Pass Criteria**: Jitter deviation $\le \pm 500$ ns compared to the reference instrument.

### 1.2 TR 101 290 Compliance
- **Test Tool**: Use "Broken Stream" samples with known P1/P2/P3 errors.
- **Verification**: Ensure all 15+ sub-errors (TS_sync, CC_error, PCR_accuracy, etc.) are correctly identified with matching timestamps and evidence metadata.

---

## 2. Domain 2: Performance Limit Proof (The 1Gbps Wall)
*Goal: Demonstrate zero packet loss at maximum network line rate.*

### 2.1 Aggregate Throughput (1Gbps / 830k PPS)
- **Environment**: 10Gbps NIC connected via loopback or high-speed switch.
- **Load Ingestion**: 128 concurrent HD streams @ 8Mbps each ($\approx 1.024$ Gbps total).
- **Monitoring**:
    - **Socket Drop**: Monitor `SO_RXQ_OVFL` (must be 0).
    - **Processing Latency**: Ensure per-packet processing time < 1μs.
- **Pass Criteria**: Zero packet loss and 100% analysis coverage at 1Gbps aggregate throughput.

### 2.2 CPU Efficiency & Affinity
- **Test**: Measure CPU scaling from 8 to 128 streams.
- **Goal**: Maintain linear scaling with < 10% overhead per core, utilizing per-thread CPU affinity to avoid context-switch jitter.

---

## 3. Domain 3: 24h Deterministic Stability (Soak Test)
*Goal: Prove that the engine is "Industrial Strength" for continuous operation.*

### 3.1 24h Resource Stability
- **Duration**: 24 hours of continuous analysis at 1Gbps.
- **Metrics to Track**:
    - **Memory (RSS)**: Must be flat (zero `malloc` in fast path).
    - **Clock Drift**: Reconstructed STC vs. Monotonic Time must remain stable.
    - **CPU variance**: < 5% variance throughout the duration.

### 3.2 Deterministic Reproducibility
- **Method**: Run the same 10-minute PCAP file 100 times.
- **Comparison**: Binary-diff the resulting JSON analysis reports.
- **Pass Criteria**: 100% bit-exact match in all counts, error timestamps, and jitter calculations.

---

## 4. Verification Workflow

### Step 1: Clean Room Setup
```bash
# Reset environment and ensure RT-kernel (if available) is active
./scripts/sys_tune_perf.sh
```

### Step 2: Run Accuracy Suite
```bash
# Compare against known 'golden' or 'broken' datasets
python3 scripts/tsa_verify_scenarios.py --dataset industrial_suite_v1
```

### Step 3: Launch 24h Stress Test
```bash
# Run 128 streams at 1Gbps for 24 hours
./scripts/extreme_stress_test.py --duration 86400 --throughput 1000
```

### Step 4: Generate Certification Report
After completion, the engine produces a `certification_report.json` containing:
- Max PPS achieved without loss.
- Max memory deviation.
- TR 101 290 coverage percentage.
- Jitter error margins.
