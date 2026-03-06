# Root Cause Analysis (RCA) Scoring Model

TsAnalyzer uses a weighted superposition of discrete causal factors to attribute faults. This provides an explainable "Banner Truth" for the NOC.

## 1. Score Calculation Logic

The engine calculates two primary scores (0.0 to 1.0) for every anomalous event.

### 1.1 Network Domain Score ($S_{net}$)
Attributes the fault to the transport path.
$$S_{net} = (0.4 \times MLR) + (0.3 \times DF) + (0.2 \times RTT_{var}) + (0.1 \times RETRANS)$$
*   **MLR**: Media Loss Rate (RFC 4445).
*   **DF**: Delay Factor (RFC 4445).
*   **RTT_var**: Variance in SRT Round Trip Time.
*   **RETRANS**: SRT Retransmission Tax.

### 1.2 Encoder Domain Score ($S_{enc}$)
Attributes the fault to the source/encoder.
$$S_{enc} = (0.3 \times PCR_{jitter}) + (0.2 \times PTS_{drift}) + (0.2 \times TSTD_{overflow}) + (0.2 \times H_{var}) + (0.1 \times Drift_{ppm})$$
*   **PCR_jitter**: Real-time jitter vs. the 27MHz Software PLL.
*   **PTS_drift**: Differential drift between audio and video PTS.
*   **TSTD_overflow**: Violations of the ISO Annex D buffer model.
*   **$H_{var}$**: Variance in Payload Entropy (detects frozen frames).
*   **Drift_ppm**: Absolute clock frequency deviation in ppm.

---

## 2. Decision Boundary Matrix

The "Banner Truth" is determined by comparing the two scores.

| Dominant Score | Final Inference | NOC Banner Text |
| :--- | :--- | :--- |
| $S_{net} < 0.2$ and $S_{enc} < 0.2$ | **Optimal** | ✅ SIGNAL OPTIMAL |
| $S_{net} > 0.6$ and $S_{enc} < 0.2$ | **Network** | ⚠️ NETWORK IMPAIRMENT |
| $S_{enc} > 0.6$ and $S_{net} < 0.2$ | **Encoder** | ☢️ ENCODER INSTABILITY |
| Both $> 0.4$ | **Complex** | 🚨 MULTI-CAUSAL CRITICAL |

---

## 3. Cross-Layer Correlation Logic

To isolate faults with 99.9% certainty, the engine correlates transport-layer behavior with content-layer symptoms (inspired by `ltntstools`).

### 3.1 The Correlation Matrix
| Transport Event | Content Symptom | Conclusion |
| :--- | :--- | :--- |
| **SRT RTT Spike** | **PCR Jitter Increase** | **Network Congestion**: Jitter is a byproduct of transport latency. |
| **Stable RTT/MLR**| **PCR Jitter Increase** | **Encoder Fault**: Clock instability is originating at the source. |
| **UDP Burst Loss** | **GOP Discontinuity** | **Signal Degradation**: Packet loss destroyed structural integrity. |
| **Zero Loss** | **Entropy Variance -> 0** | **Source Freeze**: The encoder is producing static/repeated frames. |

---

## 4. Machine Learning Ready (MLR) Data Streams

TsAnalyzer v3 targets automated predictive maintenance by providing high-density, structured data streams for external AI models.

1.  **Metric Serialization**: Every 100ms, a full state vector is pushed to the Metrics Bus.
2.  **Feature Engineering**: Raw metrics (e.g., Jitter) are pre-calculated into statistical derivatives (Moving Variance, Kurtosis, Skewness).
3.  **Labeling**: Real-time alarm states serve as "Ground Truth" labels for training failure-prediction models.

---

## 5. Explainability Audit
For every RCA decision, the engine exposes the weighted factors in the JSON report, allowing engineers to verify *why* the system made a particular attribution.
