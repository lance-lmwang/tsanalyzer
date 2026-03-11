# TsAnalyzer Pro: The Engineering Soul (Core Manifesto)

This document distills the "80% soul" of TsAnalyzer Pro. It explains the high-level architectural philosophy and the specific low-level algorithmic mastery that elevates this project to an industrial-grade broadcast metrology appliance.

---

## 1. Architectural Philosophy: The Three-Plane Y-Model

TsAnalyzer is built on a **Three-Plane Isolation** strategy, ensuring that heavy analysis or slow control operations never contaminate the time-critical data path.

### 1.1 Plane Isolation
*   **Data Plane (C-Core)**: Lock-free, zero-allocation pipeline using SPSC queues and SIMD parsing. Achieves 10Gbps+ deterministic throughput.
*   **Analysis Plane (Metrology)**: Executes the 27MHz Software PLL and T-STD simulations. Decoupled from I/O.
*   **Control Plane (Lua/REST)**: High-level orchestration, topology management, and hot-reload.

### 1.2 Dual-Mode Y-Architecture
We offer two concurrent modes sharing the same core:
*   **Mode A (Static)**: Hardcoded compliance probe for 24/7 SaaS monitoring.
*   **Mode B (Dynamic)**: Scriptable Lua gateway for intelligent routing, failover, and deep content inspection.

---

## 2. The Five Computational Pillars

### 2.1 SIMD Vectorized Parsing
We bypass byte-by-byte state machines.
*   **Vectorization**: Uses AVX2/SSE4.2 to scan 32-byte chunks for the `0x47` sync byte.
*   **Endian-Neutrality**: Performs in-register byte swapping during PID extraction at zero cost.

### 2.2 27MHz Virtual System Clock (VSTC)
Professional jitter analysis requires a reconstructed timeline.
*   **Software PLL**: A PI-controller model synchronizes the local clock to the encoder's PCR with **<10ns precision**.
*   **3D Decomposition**: Separates Jitter into Accuracy (Encoder), Drift (Frequency), and Overall Jitter (Network).

### 2.3 T-STD Digital Twin
A full mathematical simulation of ISO/IEC 13818-1 Annex D.
*   **Predictive Underflow (RST+)**: Calculates the "Remaining Safe Time" by projecting ingress rates against next-frame DTS. It warns the operator **500ms before** a decoder starve occurs.

### 2.4 Visual QoE & Shannon Entropy
*   **Information Density**: Uses Luma-plane entropy variance to detect black or frozen screens without the CPU cost of full decoding.
*   **Anti-False Positive**: Inter-frame entropy correlation distinguishes encrypted noise from valid static content.

### 2.5 Absolute-Time Pacing
*   **Precision Shaping**: Uses `clock_nanosleep(TIMER_ABSTIME)` combined with a **10us spin-wait fallback** to bypass Linux kernel scheduling jitter, ensuring MDI-DF < 1ms.

---

## 3. Industrial Robustness & Reliability

### 3.1 Worker Affinity & Cache Locality
*   **Deterministic Load**: `Worker_ID = Hash(Stream_ID) % Total_Workers`. Ensures stream context stays in L1/L2 cache, eliminating cross-core snooping latency.

### 3.2 Alert Suppression Tree
*   **Root-Cause Inhibition**: `SYNC_LOSS` suppresses all downstream errors (CC, PCR, Timeout) to prevent alert storms during physical outages.

### 3.3 Failure Containment
*   **Metrology Watchdog**: Monitors thread heartbeats. Can reset the Lua VM while keeping the C Data Plane continuity intact.

### 3.4 Determined Determinism
*   **The Contract**: `Input(Packets + HAT) + Engine(Commit) = Bit-identical JSON`. This ensures laboratory-grade reproducibility across all deployments.

---

## 4. Summary: Software-Defined Dominance
By combining the **flexibility of Lua** with the **performance of SIMD/C**, TsAnalyzer transcends traditional hardware instruments like BridgeTech VB330, offering a programmable, cloud-native future for broadcast metrology.
