# TsAnalyzer: Engineering Implementation Roadmap (Phase 1 Focus)

This roadmap defines the path to building a **Top-tier Deterministic Protocol Engine**. Success is measured by precision, throughput, and reproducibility.

---

## Phase 1: High-Performance Foundation & Precision Math
**Goal**: Zero-loss ingestion and instrument-grade PCR accuracy.

### 1.1 Ingestion Excellence
- Implement `recvmmsg` batching with L1 cache alignment for the Ring Buffer.
- Integrate `SO_TIMESTAMPING` for hardware-assisted arrival times.
- Implement per-thread CPU affinity and NUMA memory allocation.

### 1.2 Deterministic Timing Core
- Build the 27MHz System Time Clock (STC) reconstruction engine.
- Implement **Annex I PCR Jitter** algorithm (64-bit integer math).
- Validate timing stability against `CLOCK_MONOTONIC_RAW`.

---

## Phase 2: Structural Depth & Buffer Simulation
**Goal**: Full SI/PSI protocol coverage and VBV water-level modeling.

### 2.1 Recursive SI/PSI Decoding
- Implement parsers for PAT, PMT, CAT, NIT, SDT, EIT, TDT/TOT.
- Build a generic **Descriptor Dispatcher** to handle all 13818-1 and TR 101 154 tags.
- Implement stateful version tracking for all tables.

### 2.2 VBV/T-STD Simulation
- Implement the **Annex D Leaky Bucket** state machine.
- Extract DTS (Decoding Time Stamps) from PES headers to drive buffer removal events.
- Implement **RST (Remaining Safe Time)** predictive logic based on buffer occupancy.

---

## Phase 3: Forensic Output & Industrial Stability
**Goal**: Bit-exact reporting and 24h line-rate soak testing.

### 3.1 Deterministic Reporting
- Implement the **Forensic JSON** exporter (MD5-consistent).
- Implement the High-Density Industrial CLI dashboard.
- Develop the bit-exact regression suite (replay PCAP and binary-diff JSON).

### 3.2 Stability & Optimization
- **Soak Test**: 24h continuous 1Gbps run with zero RSS memory growth.
- **Micro-benchmarking**: Measure per-packet processing latency (Target < 800ns).
- **NUMA Audit**: Verify zero cross-node memory access during steady-state analysis.

---

## Phase 4: Transport Protocol Expansion (Future)
**Goal**: Extend the deterministic core to handle carrier-grade RTP and FEC auditing.

### 4.1 RTP Unwrapping (Layer 1.5)
- Implement **Zero-copy RTP parsing**: Utilize pointer offsets in `packet_descriptor` to skip 12-byte RTP headers without memory copies.
- Implement **RFC 3550 Metrology**: Real-time RTP Jitter (Network Jitter) calculation.
- Implement **Reordering Analysis**: Statistics on sequence gaps, duplicates, and required de-jitter buffer depth.

### 4.2 SMPTE 2022 Forward Error Correction (FEC)
- Implement **FEC Auditing**: Calculate **Pre-FEC vs. Post-FEC BER** (Bit Error Rate) for SMPTE 2022-5/6 streams.
- **SRT vs. RTP Comparison**: Comparative analysis of ARQ efficiency vs. raw network loss on identical physical links.

---

## Phase 5: Verification Milestones (Hard Gates)
- **Gate 1**: 1.2M PPS per core throughput at 100% analysis coverage.
- **Gate 2**: PCR jitter error margin < ±10ns compared to laboratory hardware.
- **Gate 3**: 100% consistency in JSON analysis results across 100 replay runs.
