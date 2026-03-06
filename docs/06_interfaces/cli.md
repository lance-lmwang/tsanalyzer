# High-Performance TUI (tsa_top)

The `tsa_top` utility provides a real-time, console-based monitoring interface for headless environments and local troubleshooting.

## 1. Key Operational Features

*   **Zero-Copy IPC**: Uses POSIX shared memory to read metrics from the analysis engine without triggering HTTP overhead.
*   **Dual-Line Display**: Each stream occupies two lines to balance density with readability.
*   **Status Color Coding**:
    *   🟢 **GREEN**: Signal Optimal.
    *   🟡 **YELLOW**: Degraded/Unstable.
    *   🔴 **RED**: Critical P1/P2 Failure.
    *   🔵 **BLUE**: High-bitrate dropping (Internal Bottleneck).

---

## 2. Hotkeys

| Key | Action |
| :--- | :--- |
| `q` | Quit application. |
| `s` | Sort by Stream ID. |
| `h` | Sort by Health Score (Troubled streams first). |
| `b` | Sort by Bitrate. |
| `Space` | Pause/Resume refresh. |

---

## 3. Metric Columns
*   **STREAM**: Unique label.
*   **RATE**: PCR-recovered bitrate (Mbps).
*   **HLTH**: Signal Fidelity (0-100).
*   **CC**: Accumulated Continuity Errors.
*   **JIT**: p99 PCR Jitter (ms).
*   **RST**: Remaining Safe Time (s).
