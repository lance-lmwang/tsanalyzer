# Forensic Verification Guide: Gateway Scenarios

**Goal**: Validate the gateway's ability to analyze, repair, and relay under stress.

---

## 1. Scenario: Jitter Neutralization (Analyze & Repair)
*   **Impairment**: Inject 50ms network jitter at ingress.
*   **Observation**:
    1. `MDI-DF` at ingress spikes to 50ms.
    2. TsPacer engages (status `ACTIVE_PACING`).
    3. `MDI-DF` at egress (monitored at destination) remains $< 10ms$.
*   **Success**: The gateway successfully "repaired" the jitter before forwarding.

## 2. Scenario: Critical Engine Stall (Fail-Safe Bypass)
*   **Action**: Simulate thread hang via debug API.
*   **Observation**:
    1. Gateway state flips to `BYPASS` within 10ms.
    2. Continuity Counter (CC) errors at the final destination remain at 0.
*   **Success**: Zero service disruption during analyzer failure.

## 3. Scenario: Forensic Bundle Audit
*   **Action**: Trigger critical $RST < 5s$ event.
*   **Verification Steps**:
    1. **Structure**: Download `.zip` and verify existence of `manifest.json` and `forensic_trace.json`.
    2. **Manifest**: Check `manifest.json` for `trigger_reason: RST_CRITICAL` and correct `stream_id`.
    3. **Trace**: Verify `forensic_trace.json` events show the precise timestamp when `MDI-DF` exceeded the warning threshold.
    4. **Payload**: Play `capture.ts` in VLC to correlate visual artifacts with recorded timing anomalies.
