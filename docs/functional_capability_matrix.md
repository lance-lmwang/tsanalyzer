# TsAnalyzer: Functional Capability Matrix

This document provides a comprehensive overview of the current capabilities of TsAnalyzer Pro and identifies the features currently in the development roadmap.

---

## 1. Physical & Transport Layer Telemetry

| Feature | Status | Description |
| :--- | :--- | :--- |
| **UDP/RTP Ingest** | [PASS] Implemented | High-performance raw UDP and RTP reception. |
| **SRT Caller/Listener** | [PASS] Implemented | Full support for Secure Reliable Transport protocol. |
| **PCAP Offline Replay** | [PASS] Implemented | Bit-accurate replay of network captures for forensic analysis. |
| **HLS / HTTP Ingest** | [PASS] Implemented | Libcurl-based chunk downloading and M3U8 manifest auditing. |
| **MDI (RFC 4445)** | [PASS] Implemented | Media Delivery Index (Delay Factor & MLR) using hardware timestamps. |
| **IAT Histograms** | [PASS] Implemented | Real-time Inter-Arrival Time statistical profiling for micro-burst detection. |
| **Bitrate Smoother** | [PASS] Implemented | High-precision CBR reshaping using `clock_nanosleep` absolute timing. |
| **Smart Failover** | [PASS] Implemented | Automatic switching between Primary/Backup inputs based on TR 101 290 health. |
| **I/O-Computing Decoupling** | 🔶 Partial | Hybrid Reactor model for high-density concurrent stream support. |

---

## 2. Protocol Compliance (TR 101 290)

| Feature | Status | Description |
| :--- | :--- | :--- |
| **Priority 1 (P1)** | [PASS] Implemented | Sync Loss, PAT Error, CC Error, PMT Error. |
| **Priority 2 (P2)** | [PASS] Implemented | Transport Error, CRC, PCR Repetition, PCR Accuracy, PTS Error. |
| **Priority 3 (P3)** | [PASS] Implemented | SDT and NIT version tracking/parsing. |
| **SCTE-35 Audit** | [PASS] Implemented | Nanosecond-level alignment error between ad-triggers and Video I-Frames. |
| **CAS Audit** | 📅 Planned | Monitoring of Scrambling flags and CAID descriptor compliance. |

---

## 3. Metrology & Clock Analytics

| Feature | Status | Description |
| :--- | :--- | :--- |
| **Software PLL** | [PASS] Implemented | 27MHz System Time Clock (STC) reconstruction from sparse PCRs. |
| **PCR 3D Decomposition**| [PASS] Implemented | Decomposition of jitter into Accuracy (AC), Drift (DR), and System Jitter (OJ). |
| **Walltime Drift** | [PASS] Implemented | Long-term Linear Regression (Trend) of PCR vs. Physical System Clock. |
| **PCR Root Cause** | [PASS] Implemented | Correlation logic to isolate Network vs. Encoder jitter sources. |

---

## 4. Content Layer & Elementary Stream (ES)

| Feature | Status | Description |
| :--- | :--- | :--- |
| **Zero-Copy NALU Sniffer**| [PASS] Implemented | Lightweight inspection of H.264/H.265 headers without full decoding. |
| **GOP Structure Tracking** | [PASS] Implemented | Detection of I/P/B frame sequences, GOP length (N), and GOP duration (ms). |
| **Entropy Analysis** | [PASS] Implemented | Shannon entropy variance used to detect frozen or black screens. |
| **T-STD Buffer Model** | [PASS] Implemented | Annex D simulation of TB, MB, and EB (Video) occupancy. |
| **RST (Remaining Safe Time)**| [PASS] Implemented | Predictive metric indicating time-to-underflow for decoder buffers. |
| **Thumbnail Generation** | 📅 Planned | Asynchronous sparse decoding of IDR frames. |

---

## 5. Operations & Forensic Tooling

| Feature | Status | Description |
| :--- | :--- | :--- |
| **Prometheus Exporter** | [PASS] Implemented | Native high-density metrics output for Grafana. |
| **JSON REST API** | [PASS] Implemented | Programmatic access to full engine snapshots. |
| **High-Performance TUI**| [PASS] Implemented | `tsa_top` utility for real-time console-based local monitoring. |
| **Triggered Micro-Capture**| [PASS] Implemented | Rolling 500ms ring buffer automatically saved to `.ts` on P1 triggers. |
| **Lua Extension Engine**   | [PASS] Implemented | Hot-pluggable Lua scripts for Deep Content Inspection (e.g., SCTE-35 Splice Info parsing). |
| **Alert Suppression Tree** | [PASS] Implemented | Root-cause dependency matrix (e.g., Sync Loss suppresses CC/PCR alarms) and Timer Wheel timeouts. |
| **HTML Forensic Reports** | 📅 Planned | Self-contained interactive incident replay files. |
| **Webhook Signaling** | [PASS] Implemented | Asynchronous JSON notification with retry and aggregation logic. |

---

## 6. Advanced Automation & Intelligence (Roadmap)

Inspired by enterprise-grade video management systems, these features target full-cycle automated monitoring.

| Feature | Status | Description |
| :--- | :--- | :--- |
| **Scheduled Workflows** | 📅 Planned | Calendar-based monitoring profiles (e.g., High-fidelity mode during live events). |
| **ABR Group Audit** | 📅 Planned | Measuring temporal alignment (Skew) across multiple bitrate renditions. |
| **Logo & Branding QC** | 📅 Planned | Vision-based detection of burned-in logos to ensure correct channel playout. |
| **BITC OCR Correlation** | 📅 Planned | Extracting burned-in timecodes and correlating them with PCR/PTS timelines. |
| **Enterprise Audit Trail** | [PASS] Implemented | Full lifecycle logging of stream events, config changes, and operator acknowledgments. |

---

## Legend
*   [PASS] **Implemented**: Fully functional and verified via automated tests.
*   🔶 **Partial**: Basic logic exists, but full spec coverage is pending.
*   📅 **Planned**: Designed and accepted into the roadmap, implementation pending.
*   ⏸️ **Paused**: Development suspended to focus on core metrology maturity.
