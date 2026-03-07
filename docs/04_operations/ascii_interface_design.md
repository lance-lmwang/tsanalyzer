# ASCII Interface Design Specification

This document defines the layout and mapping for the **TsAnalyzer Pro ASCII Dashboards**, used for low-bandwidth terminal monitoring and automated E2E verification.

## 1. Unified NOC ASCII View (Global Wall)

The Global Wall is designed to provide a high-density status summary of all active streams. It is implemented in `scripts/tsa_ascii_monitor.sh`.

### Layout Structure
```text
======================================================================
   TsAnalyzer Pro - UNIFIED NOC ASCII VIEW (Port 8088) - [TIMESTAMP]
======================================================================
 STREAM ID  |  STATUS  |  HEALTH  |  MOSAIC VIEW (CONCURRENCY)
----------------------------------------------------------------------
 UDP_STR_1  |  LOCKED  |   98.5%  | [####################]
 SRT_CAM_2  |  NO SIG  |    0.0%  | [--------------------]
 ...
----------------------------------------------------------------------
```

### Visual Encodings
- **STATUS**:
    - `LOCKED`: Inverse Green background (High visibility).
    - `NO SIG`: Inverse Red background.
- **HEALTH**:
    - `> 90%`: Green text.
    - `70-90%`: Yellow text.
    - `< 70%`: Red text.
- **MOSAIC BAR**: A 20-character progress bar representing health (1 `#` = 5%).

---

## 2. Terminal UI (tsa_top)

The `tsa_top` utility provides a dual-line representation per stream for deep diagnostics.

### Per-Stream Layout
```text
[ID]                [BITRATE]  [HEALTH]  [CC_ERR]  [TR_P1/2/3]  [PCR_JIT]  [MDI_DF]  [FLAGS]
  RST(N/E): [s] / [s]  Drift(S/L): [ppm] / [ppm]  RES: [WxH] @ [FPS] (GOP: [ms])
```

### Metrics Mapping
| Display Label | SHM Field | Backend Source |
| :--- | :--- | :--- |
| **BITRATE** | `current_bitrate_mbps` | `tsa_metrology_physical_bitrate_bps` |
| **HEALTH** | `master_health` | `tsa_system_health_score` |
| **CC_ERR** | `cc_errors` | `tsa_compliance_tr101290_p1_cc_errors_total` |
| **TR_P1/2/3**| `p1/2/3_errors` | `tsa_compliance_tr101290_errors` |
| **RST(N/E)** | `rst_net_s` / `rst_enc_s` | `tsa_predictive_rst_network_seconds` |

---

## 3. E2E Verification Logic

The ASCII layout is verified by capturing the output of `scripts/verify_ascii_layout_e2e.sh`. A successful capture must contain:
1. The standard header with "UNIFIED NOC ASCII VIEW".
2. At least one stream row with a valid `LOCKED` or `NO SIG` status.
3. A health percentage followed by the `[` bracket.
