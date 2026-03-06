# TsAnalyzer Metrology Model (Vimeo Pro Spec)

This document defines the synchronized sampling and calculation model for TsAnalyzer Professional. It treats the analyzer not as a statistical monitor, but as a **Software-Defined Measurement Instrument**.

---

## 1. The Software Oscilloscope Vision

TsAnalyzer operates on the principle of **Temporal Reconstruction**. It does not merely sample packets; it reconstructs the entire physical and temporal state of the delivery path.

**Packets → Temporal Mapping → Decoder Simulation → Physical State → Measurement.**

To ensure this reconstruction is deterministic and mathematically sound, the engine employs a **Sampling Barrier**.

### 1.1 Synchronized Sampling Barrier
To solve the "bitrate inversion" problem (where $\sum PID_{br} \neq Total_{br}$), all accumulators are captured behind a global barrier:
1. **Freeze**: At the start of a metrology cycle, the system "freezes" the state of all PID counters and the reference clock (PCR or System Time) simultaneously.
2. **Delta-Window**: Calculations are performed strictly on the deltas between the current freeze-frame and the previous one.
3. **Invariance**: This ensures that every bit accounted for in the $Total_{br}$ is also correctly attributed to a PID (including Null Packets 0x1FFF).

---

## 2. Metrology Domains

TsAnalyzer unifies all analysis into four deterministic domains, all sharing the same **[Virtual STC (VSTC)](./02_timing_model.md)** axis.

### 2.1 Transport Integrity Domain
- **Quantity**: Structural correctness (CC evolution, Table versioning, Scrambling transitions).
- **Principle**: Integrity violations are recorded as **state transitions** at specific VSTC points, not just counters.

### 2.2 Temporal Stability Domain (Jitter & Drift)
- **Quantity**: PCR Jitter (AC/DR/OJ) and Clock Drift (ppm).
- **Model**: Reconstructs the expected PCR progression against the observed arrival mapping.
- **Rigor**: 100% compliant with **ISO/IEC 13818-1 Annex I**.

### 2.3 Delivery Quality Domain (Network Physics)
- **Quantity**: **Media Delivery Index (RFC 4445)**.
- **DF (Delay Factor)**: Represents network-induced decoder stress, computed over the deterministic arrival window.
- **MLR (Media Loss Rate)**: Derived strictly from missing packets; no interpolation permitted.

### 2.4 Decoder Stability Domain (Predictive)
- **Quantity**: Buffer Fullness and **RST (Remaining Safe Time)**.
- **Predictive Power**: RST transforms analysis from reactive detection to **causal prediction** (time remaining before decoder failure).

---

## 3. Bitrate Calculation Tiers

| Tier | Basis | Use Case |
| :--- | :--- | :--- |
| **L1: Physical** | System Clock ($\Delta T_{sys}$) | Network interface load, MDI-DF calculation. |
| **L2: PCR-Locked** | PCR Clock ($\Delta T_{pcr}$) | **Golden Standard**. Used for CBR stability and encoder validation. |
| **L3: PID-Service** | Common Window Delta | Business-level attribution and Null Packet (0x1FFF) analysis. |

---

## 4. Measurement Precision & Stability

### 4.1 128-bit Metrology Guard
To support 100Gbps+ links and multi-day monitoring without numerical overflow:
- **Intermediate Products**: $PCR \times Bitrate$ calculations use `__int128` to maintain full precision.
- **Fixed-point Reconstruction**: All VBV simulations use **Q64.64** fixed-point math to ensure deterministic results across execution platforms.

### 4.2 Null Packet Awareness
The engine explicitly tracks PID 0x1FFF to distinguish between **Payload Bitrate** and **Padding Density**. A stable CBR stream is defined by a constant $Total_{br}$ and a fluctuating $Null_{br}$.

---

## 5. Measurement Causality Engine

A core invariant: **Every alarm MUST have a measurable physical cause.**
TsAnalyzer records the full causal path:
*Network Jitter → Arrival Dispersion → VBV Drain Acceleration → RST Collapse → Decoder Underflow.*

---

## 6. Traceability & Forbidden Practices

### 6.1 Traceability Contract
Each measurement reported by the engine is traceable down to the bit-level:
**Output Metric → Simulation State → Access Unit → TS Packet → Absolute Byte Offset.**

### 6.2 Forbidden Metrology Practices (Normative)
1. **Average away jitter**: Never smooth out transient physical violations to make graphs "look better".
2. **Hide transient errors**: Every violation must be captured, regardless of duration.
3. **Heuristic estimation**: Only modeled physics (Annex D/I) is allowed.
4. **Temporal healing**: Never "fix" or infer missing timestamps.

---

## 7. Instrument Identity

TsAnalyzer behaves as a **Software-Defined Measurement Instrument**. Its output is designed for forensic incident analysis, SLA dispute resolution, and proactive failure prevention through predictive metrology.
