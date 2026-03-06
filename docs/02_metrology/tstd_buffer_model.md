# T-STD Decoder Buffer Model & RST+

This document defines the implementation of the **Transport Stream System Target Decoder (T-STD)** model. TsAnalyzer simulates the normative decoder behavior defined in **ISO/IEC 13818-1 Annex D** to predict playback failures (freezes or crashes) before they manifest on the viewer's screen.

---

## 1. The Multi-Stage Buffer Model

The engine simulates the flow of data through the three critical stages of a standard decoder:

1.  **Transport Buffer (TB)**: Absorbs raw TS packets at the arrival rate.
2.  **Multiplexing Buffer (MB/PB)**: Handles PES reassembly and smoothing.
3.  **Elementary Stream Buffer (EB)**: The final buffer (VBV/CPB) that feeds the decoder.
    *   *Constraint*: This buffer must never underflow (starvation) or overflow (memory crash).

---

## 2. Buffer Evolution Equations

TsAnalyzer treats the decoder buffer as a deterministic physical system (Leaky Bucket).

### 2.1 Input (Arrival)
Every TS packet payload contributes to the buffer at its **Hardware Arrival Time ($HAT$)**:
$$Buffer_{level}(t) = Buffer_{level}(t-\Delta t) + Payload_{bytes}$$

### 2.2 Output (Decoding Removal)
Data is removed from the EB buffer instantaneously at the **Decoding Time Stamp ($DTS$)**:
$$Buffer_{level}(DTS_{i}) = Buffer_{level}(DTS_{i} - \epsilon) - Frame\_Size_{i}$$

### 2.3 Continuous Drain (Leak Rate)
The model assumes a constant leak rate based on the PCR-recovered bitrate ($R$):
$$Drain(t) = R \times \Delta VSTC$$

---

## 3. Predictive Metrology: RST & BSM

### 3.1 Remaining Safe Time (RST)
The "survival horizon" of the stream. It predicts the seconds remaining before the buffer is empty if input were to stop immediately.
$$RST = \frac{Buffer_{level}}{Drain\_Rate}$$

### 3.2 Buffer Safety Margin (BSM %)
BSM represents the instantaneous health of the decoder's memory relative to its normative capacity ($C$).
$$BSM \% = \left( \frac{Buffer_{level}}{C} \right) \times 100$$

**Operational Interpretation**:
| BSM % | RST Value | Risk Level | Status | Gateway Action |
| :--- | :--- | :--- | :--- | :--- |
| **> 80%** | > 15s | Low | **Optimal** | Direct Pass-through |
| **50-80%** | 10s - 15s| Medium | **Degraded** | Warn / Tag Metadata |
| **20-50%** | 5s - 10s | High | **Mitigation**| Engage Pacing Engine |
| **< 20%** | < 5s | Critical | **Critical** | Forensic Capture |

---

## 4. Handling Network Jitter

Unlike simple monitors that use average bitrates, TsAnalyzer simulation uses **Actual Arrival Timestamps**.
*   **Burst Detection**: Identifies large packet clusters that cause temporary buffer spikes (Overflow risk).
*   **Gap Detection**: Identifies network silences that lead to micro-starvation (Underflow risk) even if the average bitrate appears correct.

---

## 5. Implementation Strategy: Event-Driven Simulation

To maintain 10Gbps performance, the simulation is driven by discrete events in the Metrology Worker:
1.  **On TS Packet**: Update $Buffer_{level}$ and check for **Overflow**.
2.  **On PCR Update**: Synchronize the $VSTC$ and recalibrate the drain rate.
3.  **On Access Unit (AU) Complete**: Schedule a future **Removal Event** at the frame's $DTS$.
4.  **On Metrology Barrier**: Calculate $RST$ and $BSM$ for the current snapshot.

---

## 6. Operational Value

*   **Early Fault Prediction**: Alerting on VBV stress minutes before a buffer underflow occurs.
*   **Encoder/Mux Audit**: Detecting GOP structures that are too large for the declared VBV buffer size.
*   **Network Path Certification**: Proving that a specific IP link introduces enough jitter to crash a standard compliant decoder.
