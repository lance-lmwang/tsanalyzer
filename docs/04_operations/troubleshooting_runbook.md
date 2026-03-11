# Operational Runbook & Troubleshooting

This guide provides actionable steps for NOC operators and system administrators to diagnose and mitigate issues identified by the TsAnalyzer Pro engine.

---

## 1. Alert Dictionary & Mitigation

When an alert fires via Webhook or TUI, follow the recommended actions below:

### 🔴 P1.1: TS_SYNC_LOSS
*   **Meaning**: The 0x47 sync byte is missing or corrupted.
*   **NOC Action**:
    1. Check the physical/virtual network interface for RX errors.
    2. Verify the upstream SRT/UDP source is transmitting.
    3. Check for high CPU load causing kernel packet drops.

### 🔴 P1.4: CC_ERROR
*   **Meaning**: Packet sequence gap detected (Packet Loss).
*   **NOC Action**:
    1. If occurring on all PIDs: Suspect network congestion or policer drops.
    2. If occurring on a single PID: Suspect encoder buffer overflow or source-side multiplexing issues.

### 🟡 QOE: ENTROPY_FREEZE
*   **Meaning**: Video content is static or black.
*   **NOC Action**:
    1. Verify the source SDI/HDMI feed to the encoder.
    2. Check if the encoder is stuck on a frame (Buffer stall).

### 🔴 TSTD: PREDICTIVE_UNDERFLOW
*   **Meaning**: Decoder buffer will starve in < 500ms.
*   **NOC Action**:
    1. Check for network "micro-gaps" (silences).
    2. Verify if the stream bitrate has dropped below the nominal value.

---

## 2. Command-line Diagnostics

Use these tools to inspect the engine in real-time:

### 2.1 TSA Top (Local Dashboard)
```bash
./tsa_top
```
*   **Goal**: Identify which specific stream has high jitter or error counts.
*   **Key Indicator**: `MasterHealth < 100%` requires immediate attention.

### 2.2 System Resource Audit
```bash
# Check for kernel drops
netstat -su | grep "receive errors"

# Check for CPU pinning effectiveness
mpstat -P ALL 1
```

---

## 3. Log Analysis (Structured JSON)

TsAnalyzer logs critical events in JSON format. Search for these keys in your ELK/Splunk stack:

| Key | Description |
| :--- | :--- |
| `"level": "error"` | Internal engine failures (e.g., failed to bind port). |
| `"tag": "ALERT"` | Metrology incidents found in the stream. |
| `"event": "SYNC"` | Specific TR 101 290 P1 failure. |

---

## 4. Common FAQ

**Q: SRT connection is UP but bitrate is 0bps.**
*   **A**: Check the `streamid` in your SRT URL. If it doesn't match the configuration exactly, the gateway may accept the connection but won't route the data to an analyzer.

**Q: High CPU usage on a single core.**
*   **A**: Ensure your NIC supports RSS and that interrupts are balanced across cores. TsAnalyzer workers are efficient, but a single core handling 10Gbps of interrupts will choke.
