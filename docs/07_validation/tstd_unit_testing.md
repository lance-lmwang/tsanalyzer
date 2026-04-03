# Native T-STD Integration: Unit Testing Strategy & Validation Plan
*Production Stability & Control System Verification Standards*

## 1. Overview & Testing Philosophy
The T-STD (Transport System Target Decoder) integration is a **state-machine driven control system** containing multiple integrators (PLL, Token Pool, Program Debt). Testing must go beyond simple functional checks to verify long-term stability, conservation invariants, and frequency-domain characteristics.

**Core Testing Principles:**
- **Deterministic Evolution:** All tests must be 100% reproducible via the Virtual System Time Clock (V-STC).
- **Control System Perspective:** Verify step response, overshoot, and damping instead of just "output correctness."
- **Long-Run Evolution:** Stability is defined by the absence of drift over $10^6$ iterations.
- **Structural Invariants:** Critical constraints must hold true at every discrete time step.

---

## 2. Structural Invariant Testing (Step-by-Step Guardrails)

**Objective:** Move beyond scenario-based testing by enforcing global system constraints at every `tstd_step()`.

- **Action:** Implement an `ff_tstd_validate_invariants()` hook called at the end of every scheduler cycle.
- **Mandatory Invariants:**
  - **Buffer Level Integrity:** `0 <= buffer_level <= bucket_size_bytes` (Strict T-STD conformance).
  - **Global Token Floor:** `global_tokens >= -epsilon` (Strict rate-limiting compliance).
  - **Monotonic V-STC:** `v_stc_new > v_stc_old` (Clock domain integrity).
  - **PID Token Boundary:** `tokens_bytes >= token_floor` (Fairness borrowing limit).

---

## 3. Stability & Dynamic Response Testing

### 3.1 Integrator Drift & Long-Run Stability
**Objective:** Ensure that cumulative errors in PLL or Token integrators do not lead to long-term divergence.
- **Action:** Run $10^6$ iterations.
- **Assertions:**
  - `abs(global_tokens) < epsilon` (No token leakage/inflation).
  - `abs(program_debt) < epsilon` (No bandwidth drift).

### 3.2 Global Token Conservation Invariant
- **Invariant Formula:** `total_generated - total_consumed == global_tokens + sum(pid_tokens_relative_to_start)`
- **Assertion:** `delta(Invariant) == 0`.

### 3.3 Scheduler Oscillation Detection
- **Action:** Record `selected_program_id` sequence.
- **Analysis:** `autocorr = df['program_id'].autocorr(lag=N)`.
- **Assertion:** `autocorr < threshold`.

### 3.4 Bitrate PLL Step Response
- **Disturbance Injection:** sudden 500% burst followed by zero-essence starvation.
- **Assertions:**
  - **Recovery Time:** Bitrate returns to CBR line within $N$ steps.
  - **Overshoot:** Peak bitrate overshoot does not exceed $X\%$.

---

## 4. Property-Based Testing (Randomized Edge Discovery)

**Objective:** Use randomized input generation to discover edge-case violations that manual matrices miss (QuickCheck methodology).

- **Action:** Implement a fuzzer that generates randomized sequences of:
  - `packet_size` (varying from minimal SI to maximum PES payload).
  - `DTS_delta` (random jumps, reordering, and huge gaps).
  - `target_mux_rate` (dynamic scaling).
- **Goal:** Execute $10^5$ randomized test cycles.
- **Success Criteria:** Zero `ASSERT` failures in Structural Invariants (Section 2) across all random permutations.

---

## 5. PCR PLL Frequency Domain Analysis

### 5.1 Jitter Spectrum Verification
- **Analysis:** Fast Fourier Transform (FFT) on the `pcr_err` signal.
- **Assertions:**
  - **Low-Freq:** Controlled drift within the simulated physical envelope.
  - **High-Freq:** Noise floor matches the jitter model; no artificial harmonic spikes.

---

## 6. Stress Test Matrix (Edge-Case Coverage)

| Test Case | Scenario Description | Expected Outcome |
| :--- | :--- | :--- |
| **All-I Burst** | 10 consecutive I-frames on one PID. | Global pool handles borrowing; zero VBV violation. |
| **Ultra-Low Audio** | 32kbps AAC only at 10Mbps mux_rate. | High NULL rhythm stability; Bitrate PLL locked. |
| **High PID Count** | 100+ PIDs in a single MPTS. | Scheduler performance within budget. |
| **Zero Payload** | Long period of only NULL and PSI. | V-STC advances perfectly; clock remains deterministic. |

---

## 7. Visual CI & CI/CD Telemetry Integration

**Objective:** Enable high-signal visual feedback in the Continuous Integration pipeline.

- **Automated Artifacts:** Every CI run must attach the following diagnostic plots:
  1. **PCR Accuracy Trace:** Phase error vs. Time.
  2. **Instant Bitrate Waveform:** BPS vs. Time (CBR Straightness).
  3. **TBn Occupancy (Tektronix Style):** Buffer levels across all PIDs with violation markers.
- **CI Gate Logic:** Failure if any automated gate in `tstd_analyzer.py` returns a non-zero exit code.

---

## 8. Automated Conformance Assertions (`tstd_analyzer.py`)

```python
# Automated Conformance Report Gates
def run_validation_gates(df):
    assert df['pcr_err'].abs().max() < 500, "PCR Accuracy Violation (>500ns)"
    assert df['violation'].sum() == 0, "T-STD Buffer Violation Found"
    assert df['bps'].std() / df['bps'].mean() < 0.001, "CBR Instability detected"
    assert check_token_conservation(df), "Global Token Leakage Detected"
    print("ALL GATES PASSED: V25 Broadcast Grade Compliance Verified.")
```
