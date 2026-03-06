# TsAnalyzer Determinism Contract
## Phase 1 — Reproducible Measurement Guarantee

---

## 1. Contract Objective

The TsAnalyzer Determinism Contract guarantees that analytical results produced by the engine constitute **reproducible measurements**, not runtime observations.
For identical analytical conditions:
**Input Data + Engine Version + Configuration State = Identical Measurement Result.**

This invariant SHALL hold independent of the execution environment (CPU, OS, or load).

---

## 2. Definition of Determinism

A TsAnalyzer execution is deterministic if:
1.  Identical input packets produce **bit-identical** internal state evolution.
2.  All derived measurements occur at identical **VSTC** timestamps.
3.  Serialized output (JSON) is **byte-identical**.

Determinism applies to metric values, violation timing, causal attribution, and event ordering.

---

## 3. Engine State Closure Principle

The engine behaves as a pure mathematical state machine:
$$EngineState_{n} = f(EngineState_{n-1}, Packet_{n})$$

**Forbidden Dependencies**:
- System wall-clocks or real-time queries.
- Shared mutable global variables.
- Non-seeded randomness.
- Asynchronous race resolution or unordered data containers.

---

## 4. Replay Equivalence Theorem

**Statement**: Live Capture Execution and Offline Replay Execution SHALL be observationally equivalent.
**Rules**:
- Replay reuses recorded Hardware Timestamps and packet ordering.
- Replay mode executes the identical pipeline evaluation without analytical shortcuts.
- No temporal recomputation is permitted between Live and Replay modes.

---

## 5. Floating-Point & Serialization Policy

### 5.1 Math Policy
- **Integer Domain**: VSTC, VBV, and bitrate calculations must use **fixed-point arithmetic** (e.g., Q32.32) to eliminate platform-specific rounding variance.

### 5.2 Serialization Policy
- Output generation must guarantee **stable key ordering** and deterministic numeric formatting, independent of system locale or timezone.

---

## 6. Version & Configuration Identity

Every measurement is bound to a cryptographic identity:
**Engine Version + Git Commit Hash + Compiler Signature + Configuration Hash.**
Any measurement without this binding is considered invalid for forensic use.

---

## 7. Non-Negotiable Enforcement

**Refusal to Emit**: If the engine detects a condition that compromises determinism (e.g., hardware timestamp failure), it MUST refuse to emit measurements.
**Integrity Rule**: Incorrect certainty is worse than missing data.

---

## 8. Instrument Classification

Under this contract, TsAnalyzer behaves as a **Deterministic Software Measurement Instrument**, suitable for laboratory validation, encoder certification, and SLA forensic auditing.
