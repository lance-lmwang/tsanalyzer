# Transport Stream Entropy Analyzer

The Entropy Analyzer provides a way to detect content-layer anomalies (frozen video, black frames, encoder stalls) without performing full video decoding. It utilizes information theory to measure the **Information Density** of the TS payload.

---

## 1. The Entropy Model

Entropy ($H$) measures the randomness or complexity of the data in the PES payload.

### 1.1 Calculation
The engine calculates Shannon Entropy per PID:
$$H = - \sum_{i=0}^{255} p(x_i) \log_2 p(x_i)$$
Where $p(x_i)$ is the probability of a specific byte value (0-255) appearing in the payload.

### 1.2 Metrics
*   **Instant Entropy**: Information density of the current Access Unit.
*   **Entropy Variance ($var(H)$)**: Stability of the information flow over time.

---

## 2. Typical Entropy Levels & Diagnosis

The engine maps entropy patterns to common encoder faults:

| Condition | Entropy Level | Entropy Variance | Diagnosis |
| :--- | :--- | :--- | :--- |
| **Normal Video** | **High** | Medium | Normal information-rich content. |
| **Black Frame** | **Low** | Low | Compressed zero-data or null blocks. |
| **Static Image** | Medium | **Very Low** | Frame repetition (Slide-show or static test-pattern). |
| **Encoder Freeze**| Medium/High | **Zero** | Bit-exact frame repetition; encoder stalled. |
| **Encoder Stall** | **Sudden Drop** | High | Buffer drain failure or output logic crash. |

---

## 3. Operational Implementation

### 3.1 Sparse Sampling
To maintain 10Gbps performance, entropy is not calculated for every packet.
*   **Trigger**: Calculated on the first $N$ bytes of every detected Video IDR frame and a random subset of P-frames.
*   **Complexity**: $O(N)$ per sampled frame where $N$ is typically 4KB.

### 3.2 Freeze Detection Alarm
A **Content Freeze Alarm** is raised if:
$$var(H)_{window} < Threshold_{freeze}$$
This provides a highly reliable "Visual Confidence" metric without the CPU cost of H.264/HEVC decoding.
