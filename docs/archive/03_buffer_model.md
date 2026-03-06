# TsAnalyzer Buffer Model
## Phase 1 — Deterministic Decoder & T-STD Simulation

---

## 1. Design Objective

The TsAnalyzer buffer model reconstructs the normative decoder behavior defined by **ISO/IEC 13818-1 Annex D**. The engine SHALL simulate decoder buffer evolution as a deterministic physical system driven exclusively by reconstructed temporal data from the **[Timing Model](./02_timing_model.md)**.

---

## 2. Conceptual Foundation: The Leaky Bucket

A Transport Stream decoder is modeled as a regulated flow system:
**Transport Ingest → Decoder Buffer ($B_n$) → Timed Removal (DTS)**

TsAnalyzer executes this as a continuous leaky bucket operating on the **VSTC timeline**. The analyzer does not estimate buffer behavior; it executes it bit-for-bit.

---

## 3. Access Unit (AU) as Simulation Atom

Metrology is driven by **Access Units (AU)** reassembled from the PES layer:
```c
struct AccessUnit {
    stc_27m_t dts;           // Decoding Time Stamp
    stc_27m_t pts;           // Presentation Time Stamp
    uint32_t size_bytes;     // Total bits entering the buffer
    stc_27m_t arrival_vstc;  // Arrival instant on the VSTC axis
};
```

---

## 4. T-STD Buffer Evolution Equation

Between any two temporal points $t_1$ and $t_2$ on the VSTC axis:
$$Buffer(t_2) = Buffer(t_1) - (DrainRate 	imes \Delta VSTC) + ArrivingBits$$

### 4.1 Event Types
1.  **AU Arrival**: At $t = arrival\_vstc$, `buffer += AU_size`. Trigger **Overflow** if $buffer > capacity$.
2.  **Decoder Removal**: At $t = DTS$, decoder consumes the scheduled AU. Trigger **Underflow** if $buffer < Payload_{required}$.
3.  **Temporal Advancement**: Draining occurs continuously at the specified `drain_rate`.

---

## 5. Fixed-Point Arithmetic Mandate

To ensure bit-identical results across different CPU architectures (x86, ARM) and replay instances, **Floating-point calculations are strictly FORBIDDEN**.
- **Requirement**: Use fixed-point representation (e.g., Q32.32) for all fullness and rate calculations.
- **Goal**: Replaying the same PCAP must yield an identical buffer occupancy curve down to the last bit.

---

## 6. Remaining Safe Time (RST)

TsAnalyzer derives its primary predictive metric, **RST**, from the current buffer state:
$$RST = \frac{Buffer_{fullness}}{DrainRate}$$
- **Meaning**: Time remaining (in VSTC units) before decoder starvation.
- **Action**: Enables causal diagnostics by identifying shrinking survival horizons before a failure occurs.

---

## 7. Deterministic Event Ordering

When multiple events occur at the same VSTC timestamp, they SHALL be processed in this priority order to ensure platform independence:
1.  **Drain Evolution** (Temporal advancement)
2.  **Decoder Removal** (DTS events)
3.  **AU Arrival** (New data)

---

## 8. Buffer Invariants

1.  **Independence**: Each PID stream maintains an independent buffer state.
2.  **Forward-Only**: Simulation contains no randomness and no retroactive mutation.
3.  **Discontinuity Protection**: Buffer state resets explicitly at temporal segment boundaries.
4.  **No Smoothing**: TsAnalyzer models decoder reality — not playback resilience.
