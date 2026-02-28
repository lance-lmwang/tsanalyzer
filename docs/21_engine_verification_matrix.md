# TsAnalyzer: Phase 1 Engine Verification Matrix

This document defines the rigorous testing protocols required to prove the correctness and precision of the TsAnalyzer engine. It serves as the "Proof of Metrology" for broadcast engineers.

---

## 1. Deterministic Capture & Clock Verification

### 1.1 Packet Loss Integrity
- **Objective**: Prove zero-loss ingestion at 1Gbps.
- **Method**: Hardware traffic generator (CBR UDP) @ 1.2M PPS for 60 minutes.
- **Acceptance**: `SO_RXQ_OVFL` = 0, NIC drops = 0, CC errors = 0.

### 1.2 Clock Discipline & Monotonicity
- **Objective**: Verify STC reconstruction immunity to OS clock adjustments.
- **Method**: While running analysis, trigger an NTP step or `adjtimex` slew.
- **Acceptance**: Reconstructed 27MHz STC remains monotonic; measurement error remains ≤ ±10ns.

---

## 2. Metrology & Math Verification

### 2.1 PCR Accuracy (Annex I)
- **Objective**: Ensure instrument-grade jitter calculation.
- **Method**: Compare against hardware reference (e.g., Rohde & Schwarz / Tektronix) using a fixed PCR generator.
- **Acceptance**: Peak-to-Peak Jitter deviation ≤ ±10ns.

### 2.2 VBV Buffer Simulation (Annex D)
- **Objective**: Verify faithful implementation of the T-STD model.
- **Method**: Inject controlled bitrate spikes; compare predicted underflow time with actual decoder stall.
- **Acceptance**: Prediction error < 2% of total buffer duration.

---

## 3. ABR Synergy & Synergy Verification

### 3.1 Cross-Rendition Alignment
- **Objective**: Prove precision of the Multi-Stream Matrix.
- **Method**: Feed 8 concurrent profiles (1080p to 480p).
- **Acceptance**: IDR boundary PTS match must be 100% exact across renditions.

### 3.2 Instantaneous Bitrate (IBR) Correlation
- **Objective**: Ensure ABR profile consistency.
- **Method**: Calculate IBR at every IDR frame for all renditions.
- **Acceptance**: IBR variance across renditions at switching points must be < 5%.

---

## 4. Deterministic Replay Contract

### 4.1 MD5 Consistency
- **Objective**: Prove zero non-deterministic paths.
- **Method**: Run the same PCAP 100 times under varying system loads.
- **Acceptance**: Every output JSON must have the identical MD5 hash.

### 4.2 State-Snapshot Consistency
- **Objective**: Verify internal logic reproducibility.
- **Method**: Run at 1x, 10x, and Max speed. Compare internal VBV water-level ($B_n$) snapshots at specific packet offsets.
- **Acceptance**: Bit-exact state matching regardless of replay velocity.

---

## 5. Ground Truth Alignment (Certification)

### 5.1 Hardware Reference Benchmarking
- **Objective**: Validate TsAnalyzer results against laboratory "Gold Standards."
- **Method**: Feed a "Problematic TS" into a reference hardware analyzer (e.g., Tektronix MTS4000). Capture its PCR Jitter and Buffer graphs. Run the same file through TsAnalyzer.
- **Acceptance**: TsAnalyzer Jitter curves and VBV water-levels must correlate with reference charts within a 2% tolerance margin.

---

## 5. Stability & Resource Hygiene

### 5.1 24h Soak Test
- **Acceptance**: 24h continuous 1Gbps run; Zero RSS growth; Zero lock inflation.
