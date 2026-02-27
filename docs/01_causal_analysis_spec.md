# Causal Analysis & Predictive Models

## 1. RCA Scoring Model: Detailed Factor Mapping

TsAnalyzer uses a weighted superposition of discrete causal factors to attribute faults. Each factor ($C_i$) is normalized to a 0.0-1.0 score.

### 1.1 Causal Factor Inventory
| Factor ID | Metric | Domain Primary | Logic |
| :--- | :--- | :---: | :--- |
| **C_net_01** | `MDI-MLR > 0` | **NETWORK** | Direct evidence of transport loss. |
| **C_net_02** | `SRT_Retransmit_Tax > 10%` | **NETWORK** | Link congestion / ARQ saturation. |
| **C_net_03** | `MDI-DF Jitter Spike` | **NETWORK** | Network queuing delay. |
| **C_enc_01** | `PCR_Jitter_Peak > 2ms` | **ENCODER** | Source clock instability. |
| **C_enc_02** | `PTS_Slope_Error > 100ppm` | **ENCODER** | Encoder clock-axis drift. |
| **C_enc_03** | `T-STD_Overflow` | **ENCODER** | VBV buffer management failure. |

### 1.2 Decision Boundary Logic
- **Superposition**: $Score_{domain} = \sum (C_i \times W_i)$.
- **Domain Winner**:
    - If $Score_{net} > 0.6$ and $Score_{enc} < 0.2$ $\rightarrow$ **1: NETWORK**.
    - If $Score_{enc} > 0.6$ and $Score_{net} < 0.2$ $\rightarrow$ **2: ENCODER**.
    - If both $> 0.4$ $\rightarrow$ **3: MULTI-CAUSAL**.

---

## 2. Quantifiable Health Models

### 2.1 MDI (Media Delivery Index)
Measures the health of the IP-delivered video stream at the transport layer before it reaches the proxy egress.
* **Delay Factor (DF)**: Indicates the size of the buffer (in ms) required to neutralize network-induced jitter.
* **Media Loss Rate (MLR)**: The number of TS packets lost per second.
* **Engineering Target**: Baseline health is typically `9:0` (9ms jitter, 0 loss).

### 2.2 Remaining Safe Time (RST) Models
Predicts the "Survival Horizon" before service impact, dictating TsPacer intervention urgency.

#### 2.2.1 Network Survival ($RST_{net}$)
Predicts the seconds remaining before the inline SRT/UDP buffer depletes. This includes compensating for the gateway's internal relay latency (< 100us).
$$RST_{net} = \frac{Buffer_{current\_ms}}{EMA(Rate_{in} - Rate_{out})}$$
* *Control Action*: If $RST_{net} < 5s$, TsAnalyzer triggers TsPacer to throttle egress momentarily to rebuild the safety buffer.

#### 2.2.2 Sync Survival ($RST_{enc}$)
Predicts the seconds remaining before player-side desync due to buffer or clock drift.
$$Buffer(t) = \frac{\sum packet\_bytes}{bitrate}$$
$$RST_{drift} = \frac{Allowed\_Max\_Drift - Current\_Drift}{STC\_Drift\_Slope}$$
$$RST_{enc} = \min(RST_{drift}, RST_{tstd})$$
* *Control Action*: If $RST_{enc} < 10s$, trigger automated forensic capture.

---

## 3. The Smart Action Matrix (Inline Gateway Logic)

The intersection of RST and RCA dictates the proxy's active operational state:

| State | RST Status | RCA Fault Domain | Gateway Proxy Action |
| :--- | :--- | :--- | :--- |
| **Optimal** | $> 15s$ | `0: OK` | **Direct Pass-Through**: Lowest latency relay. |
| **Network Turbulence**| $< 10s$ | `1: NETWORK` | **Paced Relay**: TsPacer engages to smooth egress traffic. |
| **Encoder Degradation**| $< 10s$ | `2: ENCODER` | **Pass-Through + Alert**: Relay continues but marks fault. |
| **Critical Failure** | $< 5s$ | Any | **Forensic Capture**: Dumps 10s of `.ts` evidence. |

---

## 4. Fail-Safe: The Watchdog Bypass Threshold

Because the gateway operates inline, it must never become the bottleneck.
**Bypass Condition**: If processing latency $\Delta t_{proc} > 5ms$ for a contiguous window of 100 packets, the gateway panic-switches to **Transparent L4 Bypass**.
* *Impact*: Analysis is suspended, but the stream routes directly from ingress socket to egress socket via kernel-level forwarding.
