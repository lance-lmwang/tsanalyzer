# Failure Domain Inference Engine: Technical Implementation

This document formalizes the "Banner Truth" logic for the TsAnalyzer Pro NOC. It translates the mathematical models in `docs/30_causal_analysis_spec.md` into concrete **Prometheus Recording Rules** to drive the Tier-1 Failure Domain Banner.

---

## 1. Inference Strategy: Hierarchical Aggregation

The engine operates in three layers to ensure stable, high-confidence inference:
1.  **Layer 1: Normalized Signal Detectors** (0.0 – 1.0 score per factor).
2.  **Layer 2: Domain-Weighted Superposition** (Weighted sum of factors).
3.  **Layer 3: Winner-Take-All Classifier** (Final banner state string).

---

## 2. Layer 1: Causal Factor Recording Rules (0.0 - 1.0)

These rules normalize raw metrics into a uniform probability space.

```yaml
groups:
  - name: tsa_inference_l1_factors
    rules:
      # C_net_01: Media Loss Rate (MLR)
      - record: tsa_factor_net_loss
        expr: clamp_max(mdi_mlr > 0, 1.0)
      
      # C_net_02: SRT Retransmit Tax
      - record: tsa_factor_net_retransmit
        expr: clamp_max(srt_retransmit_rate / 0.20, 1.0) # 20% tax = 1.0 score
      
      # C_net_03: MDI-DF Jitter Spike
      - record: tsa_factor_net_jitter
        expr: clamp_max(mdi_df / 50, 1.0) # 50ms jitter = 1.0 score

      # C_enc_01: PCR Jitter Peak
      - record: tsa_factor_enc_pcr_jitter
        expr: clamp_max(tsa_pcr_jitter_ms / 5.0, 1.0) # 5ms PCR jitter = 1.0 score

      # C_enc_03: T-STD Buffer Overflow
      - record: tsa_factor_enc_buffer_overflow
        expr: clamp_max(tsa_tstd_overflow_count, 1.0)
```

---

## 3. Layer 2: Domain Scoring (Weighted Superposition)

Weights ($W_i$) are tuned for broadcast-grade accuracy.

```yaml
groups:
  - name: tsa_inference_l2_domains
    rules:
      # Network Score: (Loss * 0.5) + (Retransmit * 0.3) + (Jitter * 0.2)
      - record: tsa_score_network
        expr: >
          (tsa_factor_net_loss * 0.5) + 
          (tsa_factor_net_retransmit * 0.3) + 
          (tsa_factor_net_jitter * 0.2)

      # Encoder Score: (PCR Jitter * 0.6) + (Buffer Overflow * 0.4)
      - record: tsa_score_encoder
        expr: >
          (tsa_factor_enc_pcr_jitter * 0.6) + 
          (tsa_factor_enc_buffer_overflow * 0.4)
```

---

## 4. Layer 3: Final Banner Inference (The Decision)

This rule produces the `dominant_failure_domain` metric (Value Mappings in Grafana).

| Value | Inference String | Grafana Color |
| :--- | :--- | :--- |
| **0** | `SIGNAL OPTIMAL` | #1C8C5E (Green) |
| **1** | `NETWORK IMPAIRMENT` | #EAB839 (Yellow) |
| **2** | `ENCODER INSTABILITY` | #EF843C (Orange) |
| **3** | `MULTI-CAUSAL CRITICAL` | #E24D42 (Red) |

```yaml
groups:
  - name: tsa_inference_l3_decision
    rules:
      - record: dominant_failure_domain
        expr: >
          ((tsa_score_network > bool 0.6) * (tsa_score_encoder < bool 0.2) * 1)
          +
          ((tsa_score_encoder > bool 0.6) * (tsa_score_network < bool 0.2) * 2)
          +
          ((tsa_score_network > bool 0.4) * (tsa_score_encoder > bool 0.4) * 3)
```

---

## 5. Grafana Integration: Tier-1 Banner

The Tier-1 Failure Domain Banner panel MUST use the `dominant_failure_domain` metric with the following **Value Mappings**:

1.  **Map 0**: Text: `✅ SIGNAL OPTIMAL` | Color: Green
2.  **Map 1**: Text: `⚠️ NETWORK IMPAIRMENT` | Color: Yellow
3.  **Map 2**: Text: `☢️ ENCODER INSTABILITY` | Color: Orange
4.  **Map 3**: Text: `🚨 MULTI-CAUSAL CRITICAL` | Color: Red

---

## 6. Implementation Mandate: Hysteresis & Debounce

To prevent the Banner from oscillating ("flickering") during transient spikes, the Prometheus query in Grafana MUST use:
`avg_over_time(dominant_failure_domain[15s])`

This ensures that only sustained causal shifts trigger a visual state change in the NOC.
