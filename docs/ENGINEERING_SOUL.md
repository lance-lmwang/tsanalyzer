# TsAnalyzer Pro: The Engineering Soul (v3.2 Industrial Hardened)

This document distills the "80% soul" of TsAnalyzer Pro, refined by Mux Director's industrial-grade field protocols.

---

## 1. Architectural Philosophy: The Three-Plane Y-Model

### 1.1 Plane Isolation & Backpressure
*   **Data Plane (C-Core)**: Lock-free, zero-allocation pipeline.
*   **Bounded Feedback**: Analysis lag triggers a **Bounded SPSC Queue** drop policy.
*   **Backpressure Strategy**: Supports `drop_head` (for live latency) and **NULL PID Stripping** (PID 0x1FFF) to recover bandwidth during egress congestion without impacting content.

---

## 2. The Five Computational Pillars (Hardened)

### 2.1 SIMD Vectorized Parsing
*   **3-Sync Lock**: To eliminate 1/256 sync false positives, the parser validates **three consecutive 0x47 sync bytes** at 188-byte strides before locking the stream.
*   **Zero-Latency PID Extraction**: Replaced heavy `_mm256_shuffle_epi8` with a **Load-Mask-Shift-Or** pipeline, reducing instruction port pressure and micro-op latency.
*   **Ingestion Alignment**: Optimized for **192-byte padding** (TS+4B timestamp) to ensure natural SIMD alignment where possible.

### 2.2 27MHz Virtual System Clock (VSTC)
*   **PI Controller PLL**: error = $PCR - VSTC$. Frequency state adjusted using Q32.32 fixed-point for sub-nanosecond precision.
*   **Discontinuity Logic**: PCR jumps (>100ms) or wraps trigger an instant PLL re-anchoring to prevent metrology blindness.
*   **Jitter Isolation**: Employs a low-pass filtered reference to distinguish encoder-induced drift from network arrival jitter.

### 2.3 T-STD Digital Twin (Annex D)
*   **B-Frame Reorder Buffer (RoB)**: Explicitly handles the PTS/DTS reordering gap. The model ensures data removal at **DTS instant**, regardless of transmission order.
*   **AU Size SNI**: Dynamically refines buffer predictions using NAL-boundary sniffing and PES length auditing.
*   **Predictive Underflow**: $T_{underflow} = (Size_{next\_AU} - Fill) / Leak\_Rate$. Warns **500ms** before a starve.

### 2.4 Visual QoE: Hybrid Factor Analysis
*   **Freeze/Black Detection**: Uses a weighted score of **Shannon Entropy**, **Average Luma**, and **Inter-frame Variance**.
*   **Optimization**: Sampling is restricted to IDR slices only, ensuring 1000+ stream processing at <1% CPU cost.

### 2.5 Absolute-Time Pacing
*   **Hybrid Sleep/Spin**: `clock_nanosleep(TIMER_ABSTIME)` until $T-10us$, then tight spin-wait until $T$.
*   **OS Requirements**: Relies on `isolcpus` and `nohz_full` to maintain <50us scheduling determinism.

---

## 3. Lua Binding & Memory Safety

*   **Thread Isolation**: Lua VM is strictly isolated from the Data Plane.
*   **Ref-count Bridge**: C objects use atomic ref-counting bridged to Lua userdata `__gc`.
*   **Event Pooling**: Lua event objects are managed via an **Internal Object Pool** to prevent GC spikes during high-frequency alert storms.

---

## 4. Summary: The 100Gbps Future
By adopting **Consistent Hashing** for worker affinity and **NUMA-local partitioning**, TsAnalyzer scales linearly across CPU sockets, making it a software-defined peer to BridgeTech VB330 and Sencore VB440.
