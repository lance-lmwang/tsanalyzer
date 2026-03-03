# TsAnalyzer: Deterministic Transport Stream Metrology Platform

TsAnalyzer is a professional-grade **Software-Defined Measurement Instrument** designed for broadcast-grade TS analysis. It combines laboratory-grade protocol depth with high-performance real-time processing.

## 🚀 Key Features (v2.2.0 PRO)
- **Metrology Grade Precision**: Verified **8.00 Mbps CBR** accuracy on broadcast samples with < 0.001% error.
- **Deterministic Engine**: 100% Bit-identical results via 定点数 (Fixed-point) and 128-bit math.
- **Micro-Smooth Pacing**: Integrated **Token Bucket Pacer** ensuring sub-10ms CBR stability.
- **Industrial Robustness**: Dynamic heap memory model supporting high-density multi-stream analysis.
- **Real-time Forensics**: RCA scoring, TR 101 290 P1/P2 monitoring, and MDI-DF calculation.

---

## 📊 Metrology Architecture

TsAnalyzer follows a strict **7-Tier Metrology Architecture** for full-stack signal visibility, as defined in the **[NOC Dashboard Spec](./docs/44_grafana_dashboard_spec.md)**.

- **Tier 1 (Master)**: Signal Presence, Fidelity (0-100%), and Engine Determinism.
- **Tier 2 (Link)**: SRT RTT/Loss, MDI Delay Factor (DF), and Media Loss Rate (MLR).
- **Tier 3 (P1)**: TR 101 290 Priority 1 (Sync, PAT, PMT, CC Errors).
- **Tier 4 (P2)**: TR 101 290 Priority 2 (PCR Jitter, PCR Repetition, PTS Error).
- **Tier 5 (MUX)**: PID Bitrate Distribution and Null Packet Density.
- **Tier 6 (Essence)**: FPS Stability, GOP Cadence, and AV Sync (Lip-Sync Offset).
- **Tier 7 (Forensic)**: Millisecond-aligned event audit trails and RCA logs.

---

## 💻 Quick Start & Usage

### 1. Standalone Analysis (tsa)
The `tsa` CLI is used for single-stream analysis. It defaults to Prometheus metrics on port `12345`.

**Offline Replay (Maximum Speed):**
```bash
./build/tsa --mode=replay sample.ts
```
*Outputs `final_metrology.json` upon completion.*

**Real-time Monitoring (UDP/SRT):**
```bash
# UDP Multicast/Unicast
./build/tsa --mode=live --udp 1234

# SRT Listener
./build/tsa --mode=live --srt-url srt://:9000
```

### 2. Server Mode (tsa_server)
For multi-stream NOC environments, `tsa_server` manages up to 16 streams via a centralized HTTP port (`8088` by default).

```bash
# Start server with config file
./build/tsa_server tsa.conf
```

**Add Stream via API:**
```bash
curl -X POST http://localhost:8088/api/v1/config/streams \
     -H "Content-Type: application/json" \
     -d '{"stream_id":"CH-01","url":"udp://127.0.0.1:19001"}'
```

### 3. Accessing Metrics
Metrics are exported via Prometheus (default `tsa` port: `12345`, `tsa_server` port: `8088`):
- `tsa_pcr_bitrate_bps`: True content bitrate recovered from PCR clock.
- `tsa_physical_bitrate_bps`: Physical arrival rate at the network interface.
- `tsa_pcr_jitter_ms`: Microsecond-precision arrival jitter.
- `tsa_mdi_df_ms`: MDI Delay Factor (Network Jitter).

---

## 🧪 Stream Simulation (tsp)

Use the integrated `tsp` (TS Pacer) to simulate broadcast sources:
```bash
# Send file at 5 Mbps to local analyzer
./build/tsp -i 127.0.0.1 -p 19001 -b 5000000 -l -f sample.ts
```

---

## 🛠 Build & Verify
The project uses a simplified Makefile wrapping CMake commands.

```bash
make           # Build Release version (-O3)
make test      # Run all unit tests (80+ cases)
make full-test # Run Unit + Determinism + E2E tests
make rt-test   # Run Real-time Metrology Verification (30s)
```

---

## ⚖️ Determinism Contract
> *Input (Packet Sequence + HW Timestamp) + Engine version (MD5) = Bit-identical JSON Analysis.*

---

## 📖 Technical Documentation

### Strategy & Overview
- **[Product Overview](./docs/00_product_overview.md)** | **[Timing Model](./docs/02_timing_model.md)** | **[Buffer Model](./docs/03_buffer_model.md)**
- **[Determinism Contract](./docs/05_determinism_contract.md)** | **[Validation Methodology](./docs/08_validation_methodology.md)**

### Technical Specifications
- **[TR 101 290 Metrology](./docs/16_tr101290_analysis_spec.md)** | **[NOC Dashboard Spec](./docs/44_grafana_dashboard_spec.md)**
- **[High-Performance Design](./docs/34_server_high_performance_design.md)** | **[Engine Verification](./docs/21_engine_verification_matrix.md)**
