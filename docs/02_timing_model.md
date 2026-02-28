# TsAnalyzer Timing Model
## Phase 1 — Deterministic Temporal Architecture

---

## 1. Design Objective

The TsAnalyzer timing model establishes a deterministic temporal framework for Transport Stream analysis independent of operating system time and wall-clock behavior. All temporal measurements SHALL be derived exclusively from:
- hardware packet arrival timestamps (HAT)
- MPEG System Time Clock (STC)
- internally reconstructed virtual timelines (VSTC)

The engine SHALL NOT depend on human time representation or OS system time.

---

## 2. Temporal Domains

TsAnalyzer operates using three strictly separated timing domains to preserve packet causality and media synchronization.

### 2.1 Hardware Arrival Time (HAT)
- **Definition**: NIC Hardware Timestamp at DMA completion.
- **Properties**: Nanosecond precision, strictly monotonic, authoritative for packet causality.
- **Role**: Defines the physical arrival sequence.

### 2.2 System Time Clock (STC)
- **Definition**: Derived from MPEG PCR values (27 MHz).
- **Properties**: Encoder-defined timeline, independent of transport jitter.
- **Role**: Defines the decoder's internal time reference.

### 2.3 Virtual STC (VSTC)
- **Definition**: The internal analysis timeline where STC is mapped onto the Hardware Arrival Axis.
- **Role**: VSTC becomes the primary axis for all metrology, ensuring that analysis remains bit-identical whether run live or replayed.

---

## 3. Clock Separation Rules (Normative)

The following conversions are strictly FORBIDDEN to ensure determinism:
- ❌ STC → wall clock (and vice versa)
- ❌ Use of `CLOCK_REALTIME` or NTP-adjusted clocks.
- ❌ Querying system time during analysis.

**Authorized Mapping**: PCR(STC) ↔ Hardware Arrival Timeline (HAT).

---

## 4. STC Reconstruction & Interpolation

Between valid PCR samples ($PCR_1$ at $HAT_1$ and $PCR_2$ at $HAT_2$):
1.  **Interpolation Slope**: $STC_{rate} = (PCR_2 - PCR_1) / (HAT_2 - HAT_1)$
2.  **Calculation**: Intermediate values derive solely from this linear relation.
3.  **Rigor**: Must use **fixed-point arithmetic** and deterministic rounding to ensure bit-identical reconstruction across different CPU architectures.

---

## 5. Temporal Discontinuities

TsAnalyzer maintains a **Segmented Temporal Continuum**. Upon detecting a PCR reset, CC loss, or stream splice:
- The current STC segment is closed.
- A new temporal segment begins with a fresh STC reference.
- **No Smoothing**: The engine MUST NOT attempt to "heal" or interpolate across discontinuity boundaries.

---

## 6. Jitter Measurement Model

Metrology distinguishes between three orthogonal jitter sources:
- **Network Jitter**: Arrival instability (HAT variance).
- **Encoder Jitter**: PCR instability (STC deviation from nominal).
- **Decoder Stress**: AU timing variance relative to VBV/T-STD simulation.

---

## 7. Replay Temporal Equivalence

Replay mode reuses recorded Hardware Timestamps and PCR observations. To the engine, **Live Time ≡ Replay Time**. No temporal recomputation is allowed during replay, ensuring that "Re-analyzing is Re-measuring."

---

## 8. Temporal Invariants

1. Arrival order defines causality.
2. STC derives only from PCR.
3. VSTC is immutable once assigned to a packet or Access Unit.
4. Time never flows backward.
5. TsAnalyzer measures reality — it does not repair it.
