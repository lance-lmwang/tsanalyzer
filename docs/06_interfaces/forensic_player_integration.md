# Forensic Integration: Playback & Visualization

When TsAnalyzer triggers a **Micro-capture** due to a critical incident, it saves a standard `.ts` segment. This document outlines how to integrate these forensic captures into professional media workflows.

---

## 1. Playback Verification

The forensic `.ts` files are standard-compliant and can be opened by any professional media player.

### 1.1 VLC Media Player (Quick Check)
Simply open the captured file. To view metrology errors, enable the "Statistics" view in VLC to check for decoded frame drops.

### 1.2 Bitmovin Player / Web Players
For cloud-native workflows, the captured `.ts` can be registered as a single-segment HLS stream:
1.  Upload capture to S3.
2.  Generate a temporary `.m3u8` manifest.
3.  Load into the Bitmovin Player for visual audit of the freeze or macro-blocking incident.

---

## 2. Advanced Stream Analysis

For deep protocol debugging, use the following tools with the forensic capture:

### 2.1 Wireshark
TsAnalyzer preserves the original timing. Open the file in Wireshark and use the **RTP Analysis** tool to visualize jitter patterns that led to the incident.

### 2.2 DVB Inspector / MPEG-TS Utils
Since the capture contains the full P1/P2 context, use these tools to verify the private data or descriptors that caused the analytical trigger.

---

## 3. Automated Forensic Pipeline

We recommend the following Lua-based automation:

```lua
analyzer:on('SCTE35_ERROR', function(evt)
    local filename = tsa.trigger_capture(evt.stream_id, 10)
    -- Push filename to a Slack/Webhook message
    tsa.webhook_notify("Incident Captured: " .. filename)
end)
```
This ensures that every technical alert is accompanied by physical evidence for post-event root cause analysis.
