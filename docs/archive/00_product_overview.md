# TsAnalyzer: High-Performance Deterministic Protocol Engine

> **Engineering Identity**: TsAnalyzer is a professional-grade **Software-Defined Measurement Instrument**. It is built to provide bit-exact metrology for Transport Streams at 1Gbps, where every measurement is experimentally verifiable and reproducible.

---

## 1. Phase 1 Engineering Positioning

TsAnalyzer is a **High-Performance "Surgical Blade"** for Transport Streams. It enforces **Temporal Fidelity Preservation**, ensuring that the software simulation of the delivery path is immune to the non-determinism of modern OS scheduling.

### 1.1 Technical Foundations & Timing Authority
*   **Timing Authority**: All internal metrology calculations are derived exclusively from `CLOCK_MONOTONIC_RAW` or the **NIC Hardware Timestamp domain**. System wall-clock adjustments (NTP/adjtimex) are strictly excluded from analysis timing paths.
*   **Deterministic Accuracy**: Bit-exact reproducibility. Replaying a PCAP through Engine version V must yield identical results across 100 runs (MD5-consistent JSON).
*   **1Gbps Processing Architecture**: Optimized for **1.2M PPS per core** using `recvmmsg`, CPU affinity, and **NUMA-local memory allocation**.
*   **Zero-Copy Data Plane**: Utilizes a **Lock-free SPSC ring buffer** with cache-line aligned packet descriptors.

---

## 2. 4-Layer Engine Architecture

| Layer | Responsibility | Implementation Spec |
| :--- | :--- | :--- |
| **4. Deterministic Output** | Interface | Forensic JSON (Bit-exact), High-density Industrial CLI. |
| **3. Metrology Brain** | Math & Standards | **ETSI TR 101 290 (P1/P2/P3)** & **ISO/IEC 13818-1 (Annex I/D)** models. |
| **2. Structural Decode** | Protocol Depth | Full PSI/SI (PAT/PMT/NIT/SDT/EIT), Descriptor Depth, 27MHz STC. |
| **1. Ingestion Engine** | Capture | **Hardware Timestamping**, **NUMA-local lock-free SPSC Ring Buffer**. |

---

## 3. Hardcore Metrology Domains

### 3.1 ETSI TR 101 290 Compliance (The Industry Standard)
*   **Priority 1 (Availability)**: Real-time FSM for Sync_loss, PAT_error, CC_error, PMT_error, etc.
*   **Priority 2 (Quality)**: PCR_repetition, PCR_accuracy, PTS_error, CAT_error.
*   **Priority 3 (Metadata)**: NIT/SDT/EIT consistency and repetition tracking.

### 3.2 Physical & IP Domain (Line-Rate Metrics)
*   **MDI (Media Delivery Index)**: Real-time calculation of **Delay Factor (DF)** and **Media Loss Rate (MLR)** per RFC 4445.
*   **Redundancy Skew**: SMPTE 2022-7 path delay monitoring via hardware-stamped arrival offsets.

### 3.3 Timing & Buffer Domain (ISO/IEC 13818-1 Compliance)
*   **PCR Metrology**: Three-dimensional analysis including **PCR_AC (Accuracy)**, **PCR_DR (Drift Rate)**, and **PCR_OJ (Overall Jitter)** per Annex I.
*   **VBV/T-STD Simulation**: Real-time water-level tracking of the Video Pool with **DTS-level error traceability** and occupancy curve export.
*   **RST (Remaining Safe Time)**: Predictive horizon derived from buffer evolution.

### 3.4 Structural & Content Domain
*   **PSI/SI Parsers**: Recursive parsing of all mandatory DVB/MPEG tables with version tracking.
*   **Essence Metadata**: Detection of Codec, Resolution, and Frame Rate.
*   **ABR PTS/DTS Alignment**: Cross-rendition alignment matrix monitoring **PTS Drift** between profiles to ensure seamless player switching.
*   **Triggered Micro-Capture**: Automatic forensic capture of **100ms pre/post-fault raw TS packets** upon any P1 or Critical threshold violation.

---

## 4. Phase 1 Success Criteria (Verification Gates)
*   **G1 Throughput**: 1.0 Gbps aggregate throughput with zero kernel drops (`SO_RXQ_OVFL` = 0).
*   **G2 Measurement Precision**: **100% TR 101 290 P1/P2 coverage**; PCR jitter calculation matches reference hardware within ±10ns.
*   **G3 Deterministic Replay**: 100% binary identical JSON output for identical PCAP input.
*   **G4 Runtime Stability**: 24h continuous line-rate run with zero memory growth (RSS flat-line).
*   **G5 Execution Determinism**: Identical PCAP analyzed 100 times produces bit-identical output independent of background load.
