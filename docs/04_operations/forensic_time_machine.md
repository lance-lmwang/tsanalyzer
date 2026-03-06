# TS Forensic Time Machine

The TS Forensic Time Machine allows engineers to **rewind transport streams to the exact moment of failure**. Instead of analyzing only live telemetry, the system maintains a rolling historical buffer of raw packets and associated event logs.

---

## 1. Architecture

The system operates as a secondary observer on the **Metrics Bus**.

1.  **Live TS Ingress**: Incoming packets flow through the main analyzer.
2.  **Packet Capture Ring (PCR)**: A high-speed, pre-allocated circular memory buffer stores recent raw TS packets.
    *   *Default Duration*: 60 seconds.
    *   *Default Capacity*: ~50M packets (for 1Gbps aggregate).
3.  **Indexed Event Log**: Alarms and metrology spikes are cross-indexed with the byte-offset in the ring buffer.
4.  **Forensic Replay Engine**: Extracts segments from the ring buffer for post-mortem investigation.

---

## 2. Replay & Investigation Modes

When a transient fault is reported (e.g., "The viewer saw a freeze 30 seconds ago"), the engineer can query the Time Machine.

| Mode | Description | Output |
| :--- | :--- | :--- |
| **Packet Replay** | Bit-exact extraction of TS packets. | `.ts` file for Wireshark. |
| **Timeline Replay** | Re-calculation of metrics over the window. | High-res Jitter/Bitrate charts. |
| **Frame Replay** | Sparse decoding of the cached segment. | Visual frame sequence. |

---

## 3. Investigation Workflow Example

**Scenario**: Viewer reported a freeze at 20:11:43.

1.  **Selection**: Operator selects the $T \pm 5s$ window around the report.
2.  **Trace Analysis**:
    *   `20:11:39`: PCR Jitter spike detected (Root Cause: Network).
    *   `20:11:40`: UDP Burst Loss detected (MDI-MLR spike).
    *   `20:11:41`: TR 101 290 Continuity Errors on Video PID.
    *   `20:11:43`: T-STD Underflow (Decoder stall).
3.  **Conclusion**: Transient network microburst caused packet loss leading to decoder starvation.

---

## 4. Resource Guardrails

To ensure that the Time Machine does not impact real-time analysis:
*   **Zero-Copy Write**: The Capture Ring is written to via DMA-like pointer transfers.
*   **Asynchronous Disk Flush**: When an event triggers a "Save to Disk," the IO is handled by a background worker with restricted priority.
