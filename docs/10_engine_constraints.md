# TsAnalyzer Engine Constraints
## Phase 1 — Non-Negotiable Architectural Constraints

---

## 1. Objective

This document defines architectural constraints required to preserve TsAnalyzer as a deterministic measurement instrument. These constraints are **permanent engineering laws**, not implementation guidelines. Violation risks invalidating measurement correctness.

---

## 2. Immutable Engine Principles

The following properties are non-negotiable and MUST be preserved through all future updates:
1.  **Deterministic Execution**: Output must depend only on input and version.
2.  **Monotonic Timing**: STC must be reconstructed without OS time reference.
3.  **Causal Evolution**: State transitions must be forward-only and traceable.
4.  **Zero-Loss Ingestion**: Sustained 1Gbps line-rate processing is a correctness gate.
5.  **Reproducible Replay**: Replay result must equal live capture result bit-for-bit.

---

## 3. Data Plane & Memory Constraints

- **Zero-Copy**: Packets SHALL NOT be copied within the analytical pipeline. Ownership moves via pointer transfer only.
- **Linear Ownership**: A packet SHALL have exactly one writer at any time. Bidirectional access across stages is forbidden.
- **NUMA Locality**: All buffers and threads MUST remain NUMA-local to the NIC. Cross-socket memory access is prohibited in certified mode.
- **Memory Wall**: Startup allocation only. Zero `malloc/free` in the fast path.

---

## 4. Execution & Timing Constraints

- **Fixed-Function Pipeline**: Topology (Capture → Decode → Metrology → Output) is static. No dynamic worker pools or async task schedulers.
- **CPU Affinity**: Threads MUST remain pinned to isolated cores.
- **Forbidden Clocks**: No use of `CLOCK_REALTIME`, wall-clock, or application-layer timers for analytical logic.
- **Numerical Integrity**: Critical metrology MUST NOT rely on floating-point arithmetic. Fixed-point (int64) is mandatory for STC and VBV.

---

## 5. Operational Integrity Constraints

- **No Silent Recovery**: The engine SHALL NEVER conceal packet loss or resynchronize silently.
- **No Interpolation**: Missing timestamps or data SHALL NOT be guessed or estimated.
- **Replay Equivalence**: Replay MUST use identical analytical paths as live capture. No analytical shortcuts in replay mode.
- **Configuration Boundary**: Analyzing parameters cannot be changed "hot." Configuration mutation requires a pipeline reset or epoch boundary.

---

## 6. Dependency & Evolution Rules

- **Library Transparency**: External libraries MUST NOT introduce hidden threading, background timers, or non-deterministic memory behavior.
- **Responsibility Rule**: Every line of code may affect measurement truth. Changes must be justified in metrological terms.
- **The Final Rule**: If an "optimization" increases performance but reduces determinism, it MUST be rejected. Integrity supersedes performance.
