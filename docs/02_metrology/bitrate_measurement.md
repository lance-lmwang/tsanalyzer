# Bitrate Measurement & Metrology Standards

This document defines the high-precision bitrate measurement architecture of TsAnalyzer, designed to align with carrier-grade industrial standards.

## 1. Physical Bitrate (Total TS Bitrate)

The Physical Bitrate represents the raw throughput of the Transport Stream (Layer 2).

### Definition
Unlike network-layer throughput, **Total TS Bitrate** counts only the valid 188-byte TS packets. It includes:
*   Video/Audio/Data PIDs.
*   SI/PSI Tables (PAT, PMT, SDT, etc.).
*   Null Packets (PID 0x1FFF).
*   **Excludes**: IP, UDP, and Ethernet headers.

### Mathematical Formula
$$Bitrate_{bps} = \frac{\Delta UniquePackets \times 1504}{\Delta WallClock_{ns} \times 10^{-9}}$$
*(Where 1504 is 188 bytes $\times$ 8 bits)*.

### Implementation & De-duplication
To handle complex network environments like PCAP loopback captures or multi-path UDP ingress, the engine implements a **PID + Continuity Counter (CC)** de-duplication mechanism at the entry point (`tsa_process_packet`).
*   **Mirrored Packets**: Packets with the same PID and CC arriving in rapid succession are ignored.
*   **Counting**: Only unique packets increment the monotonic `total_ts_packets` counter used for physical metrology.

---

## 2. Business Bitrate (PCR Content Bitrate)

The Business Bitrate represents the constant bitrate (CBR) of a specific program within the multiplex.

### Multi-Program Isolation (MPTS)
In an MPTS stream, each program has its own PCR (Program Clock Reference). TsAnalyzer isolates these into independent **Clock Domains**:
*   **Per-PID Context**: Each PID tracks its own `pkts_since_last_pcr` and `last_pcr_ticks`.
*   **Atomic Sampling**: Packet counts and PCR values are sampled atomically to ensure that the numerator (bits) and denominator (time) are perfectly synchronized.

### Calculation Logic
Instead of wall-clock time, the Business Bitrate uses the 27MHz PCR clock:
$$Bitrate_{bps} = \frac{\Delta Packets \times 1504 \times 27,000,000}{\Delta PCR_{ticks}}$$

### Aggregation
The global `pcr_bitrate_bps` reported in the stream summary is the **sum of all unique program bitrates**. This prevents program collision and correctly reflects the total occupancy of the payload.

---

## 3. Multi-Program (MPTS) Handling Logic

TsAnalyzer treats an MPTS stream as a collection of independent, concurrent clock domains. To ensure accurate metrology for complex multiplexes, the following strategies are enforced:

### 3.1 PCR Context Isolation
Every program in the multiplex is analyzed within its own isolated context:
*   **Decoupled Tracking**: PCR values from Program A do not interfere with the timing baseline of Program B.
*   **Program-Specific Bitrate**: The bitrate for each program is calculated using the specific PCR PID associated with its PMT. This results in high-precision per-program metrics.

### 3.2 Aggregated Business Throughput
While each program is monitored individually, the global `Business Bitrate` (reported as `pcr_bitrate_bps` in snapshots) is the **algebraic sum of all recognized program bitrates**.
*   This aggregate value represents the total effective payload occupancy of the transport stream.
*   By summing independent PCR-derived rates, the system naturally filters out null packets and jitter artifacts that affect global wall-clock measurements.

### 3.3 Integrity Correlation
In an MPTS environment, the `Physical Bitrate` (Layer 2) and the `Aggregated Business Bitrate` (Layer 3) are correlated. Any significant delta (e.g., > 5%) is flagged as a potential **Multiplex Inconsistency**, indicating ghost traffic or excessive null stuffing.

---

## 4. Implementation Guardrails

### Clock Domain Isolation
**Critical Rule**: Never mix PCR Ticks (27MHz) and System Nanoseconds (1GHz) in the same calculation.
TsAnalyzer enforces strict separation:
*   **Physical Bitrate**: Uses `CLOCK_MONOTONIC` for $\Delta t$.
*   **PCR Bitrate**: Uses PCR Ticks for $\Delta t$.

### Stability & Smoothing
To provide a professional "broadcast equipment" feel, TsAnalyzer applies different smoothing strategies:
*   **Physical Tier**: Heavy EMA smoothing (e.g., 20% instant / 80% historical) with a mandatory 500ms minimum sampling window to filter out OS scheduling jitter.
*   **Business Tier**: Light or no smoothing to accurately reflect the instantaneous CBR quality and encoder performance.

### PCAP & Loopback Special Handling
When capturing on `lo` or promisc interfaces, `pcap_setdirection(PCAP_D_IN)` is used to ignore local outbound traffic, preventing the "bitrate doubling" effect common in development environments.
