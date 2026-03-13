# Specification: Industrial Supremacy (ltntstools Beyond)

## 1. Background
The `ltntstools` and `libltntstools` libraries represent a significant milestone in open-source TS analysis, particularly excelling in deep business-layer inspection (SCTE-35, Captions) and fine-grained TR 101 290 edge-case handling. However, their reliance on legacy concurrency models (mutexes, linked lists) and heavy memory allocation (`calloc`/`free` per packet) severely bottlenecks their performance in high-density, multi-stream cloud environments.

## 2. Objective
This track is designed to completely absorb the functional advantages of `ltntstools` while surpassing its limitations through TsAnalyzer's modern, lock-free, zero-allocation architecture.

The ultimate goal is to establish TsAnalyzer as the undisputed industrial standard by achieving:
1.  **Predictive T-STD Buffer Modeling:** Moving beyond static clock comparison to dynamic buffer overflow/underflow prediction.
2.  **Intelligent Alert Suppression:** Implementing an alert dependency tree to eliminate alarm storms caused by cascade failures (e.g., Sync Loss triggering PCR/CC errors).
3.  **Lua-Powered Deep Inspection:** Replacing hardcoded C-struct parsing with dynamic, hot-pluggable Lua scripts for extracting SCTE-35 and Captions, offering unmatched flexibility.

## 3. Scope & Requirements

### 3.1 T-STD Predictive Buffer Model
*   Extend `tsa_clock_domain.c` and `tsa_es_track.c`.
*   Implement mathematical models for the Video Buffering Verifier (VBV) and Coded Picture Buffer (CPB).
*   **Requirement:** The engine must predict an impending buffer underflow/overflow at least 500ms before it occurs based on the ingress byte rate and current PTS/DTS consumption.

### 3.2 TR 101 290 Alert Suppression Tree
*   Introduce a global timing wheel for handling low-frequency and timeout events (e.g., 0 bps stream drops leading to PAT/PMT timeouts) without relying on incoming packets to drive the state machine.
*   Implement a dependency graph in `tsa_alert.c`:
    *   `TS Sync Loss` -> Suppresses `CC Error`, `PCR Error`.
    *   `CC Error Burst` -> Suppresses `PCR Jitter` (as missing packets inherently cause jitter).
*   **Requirement:** Decrease alert noise by at least 80% during simulated physical-layer network disconnections.

### 3.3 Lua-based Business Layer Analysis
*   Develop generic `SectionExtractor` and `PesExtractor` payloads in C that safely bridge raw memory to the Lua execution context.
*   Utilize `tsa_lua.h` to process PID Type `0x86` (SCTE-35) and user-data (Captions).
*   **Requirement:** Provide out-of-the-box Lua scripts that decode SCTE-35 Splice Info Sections and log them to the expert analysis dashboard, matching or exceeding `ltntstools` capability without bloating the C binary.