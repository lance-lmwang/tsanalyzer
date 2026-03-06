# RST+ and Buffer Safety Models

This document defines the predictive telemetry used to anticipate viewer impact before physical buffer exhaustion occurs.

## 1. Remaining Safe Time (RST)

RST is the primary predictive metric representing the "survival horizon" of the stream.

### 1.1 Network RST ($RST_{net}$)
Predicts seconds remaining before inline network buffers (SRT/UDP) deplete.
$$RST_{net} = \frac{Buffer_{curr\_bytes}}{EMA(Rate_{in} - Rate_{out})}$$

### 1.2 Encoder RST ($RST_{enc}$)
Predicts seconds remaining before decoder starvation (Underflow) or crash (Overflow) based on the T-STD model.
$$RST_{enc} = \min(RST_{underflow}, RST_{overflow})$$

---

## 2. Mux Buffer & Burst Model (DVB T-STD)

TsAnalyzer simulates the **virtual decoder buffer** defined in DVB T-STD to detect multiplexing anomalies.

### 2.1 Buffer Evolution Formula
For every incoming TS packet:
*   **Fill**: `buffer_level += packet_size`
*   **Drain**: `buffer_level -= drain_rate * delta_time` (where drain_rate is the ES bitrate)
*   **Constraint**: $0 \le buffer\_level \le buffer\_size$

### 2.2 Burst Pattern Detection
The engine identifies **Burst Clusters** (large packet groups followed by silence):
*   **Root Causes**: Encoder mux scheduling errors, statistical multiplex imbalance, or network burst shaping.
*   **Operational Risk**: Sudden buffer spikes leading to imminent **Overflow** risk.

---

## 3. Buffer Safety Margin (BSM)

While RST provides a time-based horizon, the **Buffer Safety Margin %** provides an instantaneous health ratio.

### 2.1 Definition
BSM represents the current buffer occupancy relative to its normative target or capacity.
$$BSM \% = \left( \frac{Buffer_{current}}{Buffer_{target}} \right) \times 100$$

### 2.2 Operational Interpretation
*   **100%**: Perfectly aligned with the T-STD model.
*   **< 20%**: Critical Starvation Risk. High probability of frame drops.
*   **> 150%**: Critical Overflow Risk. Decoder memory pressure.

---

## 3. Thresholds & Action Matrix

| RST Value | Risk Level | Status | Gateway Action |
| :--- | :--- | :--- | :--- |
| **> 15s** | Low | **Optimal** | Direct Pass-through |
| **10s - 15s** | Medium | **Degraded** | Warn / Tag Metadata |
| **5s - 10s** | High | **Mitigation**| Engage Pacing Engine |
| **< 5s** | Critical | **Critical** | Forensic Capture |

---

## 4. The Predictive Horizon Chart

By combining RST and BSM, TsAnalyzer generates a **Predictive Horizon Chart**:
*   **X-Axis**: Time (forward-looking).
*   **Y-Axis**: Predicted Buffer Occupancy.
*   **Alert Trigger**: An alarm is raised if the projected curve intersects the 0% (Underflow) or 100% (Overflow) boundary within the next 5 seconds.
