# Metrology Verification & Regression Process

TsAnalyzer Pro employs a rigorous verification pipeline to ensure that architectural changes do not compromise metrological integrity.

---

## 1. The "Golden Stream" Repository

A **Golden Stream** is a curated collection of MPEG-TS samples (PCAP/TS) with known, pre-calculated ground truths.

### 1.1 Categories
*   **Compliance (TR 101 290)**: Streams with deliberate, frame-accurate errors (e.g., CC gaps, Sync loss, PCR jumps).
*   **High-Dynamics (Statistical Mux)**: Streams with extreme bitrate swings to test buffer modeling.
*   **Forensic (Encrypted/Scrambled)**: Tests for CA/DRM signaling and entropy analysis.
*   **Clock-Drift (27MHz)**: Long-duration captures used to verify PLL stability over 24+ hours.

### 1.2 Access & Storage
Golden streams are stored in the `/sample` directory (for smoke tests) and a dedicated **Metrology Data Lake** (for full regression).

---

## 2. Deterministic Regression Testing

Because the engine is deterministic, we utilize **Hash-Matched Verification**.

### 2.1 The Verification Loop
1.  **Baseline Generation**: Run a stable version of the engine against the Golden Streams to generate "Golden JSON" reports.
2.  **Candidate Execution**: Run the new build (PR) against the same streams.
3.  **Diffing**: Perform a deep JSON diff of the outputs.
4.  **Zero-Tolerance**: Any change in a numerical value (e.g., PCR Jitter, Bitrate) that is not explicitly expected constitutes a **Metrology Regression**.

### 2.2 Automation via `tsa_verify_roadmap.py`
The `scripts/tsa_verify_roadmap.py` tool automates this process:
```bash
python3 scripts/tsa_verify_roadmap.py --engine ./build/tsa_cli --data /mnt/golden_streams/
```

---

## 3. Real-Time Hardware-in-the-Loop (HIL)

For hardware-related features (AF_XDP, Hardware Timestamping), we employ a HIL testbed.

### 3.1 Test Topology
*   **Traffic Generator**: Spirent or dedicated high-pacing FPGA cards.
*   **DUT (Device Under Test)**: TsAnalyzer Pro running on target server hardware.
*   **Monitoring**: Secondary probe to verify the **Smart Gateway's** reshaping accuracy.

### 3.2 Success Criteria
*   **Pacing Precision**: Output packet departure $\Delta t_{err} < 500ns$ compared to the ideal T-STD schedule.
*   **Zero-Copy Efficiency**: CPU utilization remains linear regardless of packet small-batching.
