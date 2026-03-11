# Metadata Correlation: Transport Stream vs. Manifest

A common challenge in OTT workflows is ensuring that ad-insertion markers (SCTE-35) embedded in the Transport Stream are correctly translated into HLS/DASH manifest tags. TsAnalyzer provides nanosecond-level auditing for this cross-layer synchronization.

---

## 1. The Correlation Logic

TsAnalyzer audits the delay between the physical arrival of a Splice Info Section and its expected execution time.

### 1.1 From TS to JSON
When an `0xFC` section is detected, TsAnalyzer extracts:
*   **Splice PTS**: The exact 33-bit timestamp for the transition.
*   **Associated IDR**: The nearest video Keyframe PTS.

### 1.2 Matching with HLS Tags
The monitoring stack (e.g., a custom Lua script or an external orchestrator reading the REST API) should match the TsAnalyzer output against the HLS Manifest:

| TsAnalyzer Event | HLS Manifest Tag | Correlation Key |
| :--- | :--- | :--- |
| `SCTE35_SPLICE_INSERT` | `#EXT-X-DATERANGE` | `PLANNED-DURATION` vs. Splice Command |
| `IDR_DETECTION` | `#EXT-X-PROGRAM-DATE-TIME` | Wall-clock to PTS mapping |

---

## 2. Auditing Alignment Errors

TsAnalyzer reports the **Alignment Error ($\Delta_{align}$)**:
$$\Delta_{align} = |PTS_{IDR} - PTS_{SCTE35}|$$

*   **Optimal**: $\Delta_{align} < 1ms$. (The ad-break starts exactly on a Keyframe).
*   **Warning**: $1ms < \Delta_{align} < 500ms$. (The player may experience a glitch or black frame during transition).
*   **Critical**: $\Delta_{align} > 500ms$. (The manifest tag and the physical stream are out of sync).

---

## 3. Lua Automation Example

```lua
analyzer:on('SCTE35', function(evt)
    local err_ns = evt.alignment_error_ns
    if err_ns > 10000000 then -- 10ms
        tsa.log("WARNING: Manifest/Stream alignment jitter detected: " .. err_ns .. " ns")
    end
end)
```
