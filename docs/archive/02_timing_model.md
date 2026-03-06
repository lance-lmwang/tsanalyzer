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
3.  **Rigor**: Uses **__int128 fixed-point arithmetic** to ensure overflow protection and bit-identical reconstruction across architectures.

### 4.1 PCR Clock Inspector (27MHz Base)
TsAnalyzer employs a dedicated **ClockInspector** module for each PCR PID to perform high-fidelity physical layer analysis:
- **Reconstruction**: Recombines the 33-bit `base` and 9-bit `extension` into a continuous 42-bit 27MHz timeline.
- **Wrap-around Handling**: Correctly handles the 42-bit PCR rollover (approx. every 26.5 hours).
- **Drift Compensation**: Periodically resets the reference baseline (every 1000 PCRs) to distinguish between short-term **Jitter** and long-term **Drift** caused by local system clock PPM errors.

### 4.2 Overall Jitter (PCR_OJ) Formula
The Overall Jitter is calculated by comparing the actual PCR arrival with the expected arrival based on the monotonic system clock:
$$Jitter = (PCR_{curr} - PCR_{ref}) - \frac{(HAT_{curr} - HAT_{ref}) \times 27,000,000}{1,000,000,000}$$
Where $HAT$ is the Hardware Arrival Time in nanoseconds.

---

## 5. High-Precision Pacing Model (Pacer Side)

TsAnalyzer's pacer (`tsp`) implements a **Hybrid Deterministic Scheduler**:
- **Strategy**: Wait time > 2ms → `clock_nanosleep(TIMER_ABSTIME)`; Wait time < 2ms → `busy-wait + pause`.
- **Clock Source**: `CLOCK_MONOTONIC` to ensure immunity to system time adjustments.
- **Burst Control**: Token bucket depth is capped at **10ms** of bitrate traffic to suppress micro-bursts and preserve NIC buffer integrity.

---

## 6. Arrival Time Normalization (Analyzer Side)

To eliminate artifacts from OS/NIC batching (where multiple packets arrive in a single nanosecond, especially on `localhost`):
- **Dynamic Spreading**: The engine automatically spreads packets arriving at the same timestamp based on the **Instantaneous Physical Bitrate**.
- **Calculation**: $Gap_{ns} = (Packet_{bits} * 1e9) / Bitrate_{bps}$.
- **Effect**: Restores physical plausibility to IP-Jitter measurements by realigning the arrival axis with real-world transmission physics.

---

## 7. Temporal Discontinuities

TsAnalyzer maintains a **Segmented Temporal Continuum**. Upon detecting a PCR reset, CC loss, or stream splice:
- The current STC segment is closed.
- A new temporal segment begins with a fresh STC reference.
- **No Smoothing**: The engine MUST NOT attempt to "heal" or interpolate across discontinuity boundaries.

---

## 8. Jitter Measurement Model

Metrology distinguishes between three orthogonal jitter sources:
- **Network Jitter**: Arrival instability (HAT variance).
- **Encoder Jitter**: PCR instability (STC deviation from nominal).
- **Decoder Stress**: AU timing variance relative to VBV/T-STD simulation.

---

## 9. Replay Temporal Equivalence

Replay mode reuses recorded Hardware Timestamps and PCR observations. To the engine, **Live Time ≡ Replay Time**. No temporal recomputation is allowed during replay, ensuring that "Re-analyzing is Re-measuring."

---

## 10. Temporal Invariants

1. Arrival order defines causality.
2. STC derives only from PCR.
3. VSTC is immutable once assigned to a packet or Access Unit.
4. Time never flows backward.
5. TsAnalyzer measures reality — it does not repair it.
