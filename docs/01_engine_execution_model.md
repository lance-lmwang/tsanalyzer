# TsAnalyzer Engine Execution Model
## Phase 1 — Deterministic Runtime Architecture

---

## 1. Design Objective

The TsAnalyzer execution model guarantees that Transport Stream analysis results are bit-reproducible and depend exclusively on:
- packet payload content
- packet arrival order
- engine binary version
- configuration hash

The execution result SHALL remain invariant against OS scheduling behavior, CPU migration, wall-clock adjustments, and background system activity. The runtime operates as a **deterministic dataflow machine**, not a time-driven application.

---

## 2. Determinism Boundary

### 2.1 Deterministic Input Definition
The deterministic boundary begins at: **NIC Hardware Timestamp + DMA Completion Order**.
Everything upstream of this boundary is considered non-deterministic physical reality. Everything downstream MUST be deterministic.

### 2.2 Deterministic Execution Contract
For identical (Input Stream, Engine Version, Configuration), the engine MUST produce **Bit-identical Analysis Output** across different machines, CPU vendors, and execution times.

---

## 3. Core Execution Principles

1.  **Order Determinism**: Packets SHALL be processed strictly in capture order.
2.  **Linear Ownership**: Packet ownership SHALL transfer exactly once. Shared mutable packet memory is forbidden.
3.  **Causality Isolation**: All computation is derived from internal timeline domains.
4.  **Immutability**: Processing stages operate on immutable views of the packet data.
5.  **Forward-Only**: Execution flow SHALL be forward-only (no retroactive mutation).

---

## 4. Thread Topology: Fixed Function Pipeline

TsAnalyzer employs a static functional pipeline with CPU affinity locking.

```
NIC RX Queue
    ↓
[Capture Thread]      Core N
    ↓ SPSC
[Decode Thread]       Core N+1
    ↓ SPSC
[Metrology Thread]    Core N+2
    ↓ SPSC
[Output Thread]       Core N+3
```

**Guarantees**: No work stealing, no dynamic scheduling, and no thread migration after initialization. Pipeline latency MAY vary; computation outcome MUST NOT.

---

## 5. Memory Ownership Model

### 5.1 Linear Ownership Rule
Packet memory moves strictly: NIC DMA → RX Slot → Descriptor → Decode → Metrology → Serialization → Release.
- **Copying**: Forbidden.
- **Mutation**: Single-owner only.
- **Back-reference**: Forbidden.

### 5.2 Memory Wall
After ownership transfer, the previous stage loses write permission logically. Packets SHALL NEVER cross stages bidirectionally, establishing a memory visibility wall.

### 5.3 Cache & NUMA Determinism
All runtime buffers SHALL be allocated on NIC-local NUMA nodes and remain CPU-local. Descriptors are aligned to 64-byte cache-line boundaries to prevent false sharing.

---

## 6. Timing & Clock Domains

- **Allowed**: ✅ NIC Hardware Timestamp, ✅ `CLOCK_MONOTONIC_RAW`.
- **Forbidden**: ❌ `CLOCK_REALTIME`, ❌ NTP-adjusted clocks, ❌ wall-clock conversions, ❌ system time queries during analysis.

---

## 7. STC Reconstruction Model

The 27 MHz System Time Clock (STC) forms the primary analysis axis.
- **64-bit Strictly Monotonic**: Reconstructed from PCR samples and interpolated using hardware arrival spacing.
- **Independent**: Immune to OS temporal state. No temporal correction or smoothing is permitted.

---

## 8. Metrology Execution: Atomic Access Unit Model

### 8.1 Access Unit Definition
The Decode stage produces immutable **Access Units (AU)**:
```c
struct AccessUnit {
    stc_27m_t dts;
    stc_27m_t pts;
    size_t size;
    uint64_t arrival_vstc;
};
```
Access Units represent the smallest deterministic simulation input.

### 8.2 Deterministic VBV Simulation
For each AU: `buffer += AU_size` and `buffer -= drain_rate * Δt`.
- **Fixed-point arithmetic only**.
- Architecture-independent math behavior.
- No floating-point dependency.

---

## 9. Replay Equivalence Model

Replay execution SHALL be mathematically equivalent to live capture.
**PCAP + Engine Version + Config Hash = Bit-identical Output.**
Live and replay modes differ only in the ingestion source; both reuse recorded hardware timestamps and execute identical pipeline evaluation.

---

## 10. Failure Containment & Forensic Integrity

TsAnalyzer operates under a forensic preservation model.
Upon ingestion loss detection:
- Continuity counter gaps recorded.
- Byte offset preserved.
- Timeline flagged **DEGRADED**.
- Prohibited: Silent resynchronization, packet synthesis, or temporal healing.

The engine MUST expose transport defects rather than compensate for them.
