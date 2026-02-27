# Expert Analysis Specification

## 1. ES (Elementary Stream) Parsing
Automated scanning of PES payloads to extract codec-level metadata.

### 1.1 Support Matrix
- **H.264 (AVC)**: SPS/PPS parsing for resolution, profile, and level. NALU type tracking for GOP structure.
- **H.265 (HEVC)**: VPS/SPS parsing.
- **AAC**: ADTS header parsing for sampling rate and channel configuration.

### 1.2 GOP Analytics
- **GOP Length (N)**: Number of frames between IDR frames.
- **B-frame Distance (M)**: Consecutive B-frame count.
- **Consistency**: Real-time jitter detection in GOP structure.

## 2. T-STD Buffer Modeling
Simulation of the three-stage decoder buffer model per ISO/IEC 13818-1.

### 2.1 Buffer Stages
- **TB (Transport Buffer)**: 512-byte boundary processing at peak rate.
- **MB (Multiplexing Buffer)**: Smoothing buffer simulation using leakage rate $R_{X_n}$.
- **EB/B (Elementary Stream Buffer)**: Final ES buffer with DTS-triggered removal.

### 2.2 Metrics
- **Occupancy (%)**: Real-time fullness ratio for each buffer stage.
- **Risk Score**: Prediction of Underflow (buffer starvation) or Overflow (decoder crash).
