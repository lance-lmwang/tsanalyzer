# Metrology Alarm Engine

TsAnalyzer Pro features a carrier-grade, stateful alarm engine designed to eliminate alert fatigue and ensure high-reliability signaling.

---

## 1. Stateful Monitoring Model (Edge-Triggered)

Instead of sending an alert for every error event, TsAnalyzer utilizes an internal **Edge-Triggered State Machine**. This separates the detection of an impairment from the notification policy.

### 1.1 Alert Lifecycles
*   **OFF (Normal)**: The default state.
*   **FIRING (Alert Active)**:
    *   Triggered on the **first occurrence** of a critical error (e.g., P1.1 Sync Loss).
    *   An immediate `CRITICAL` notification is dispatched.
*   **RESOLVED (Cleared)**:
    *   Triggered only after the error has completely disappeared and the stream has remained stable for a **5-second observation window**.
    *   An `OK` notification is dispatched, and the state returns to `OFF`.

---

## 2. High-Reliability Webhook Signaling

The system features a dedicated, non-blocking signaling thread to ensure that alarm delivery never impacts real-time stream analysis.

### 2.1 Resilient Delivery
*   **Async Task Queue**: Up to 1024 pending alerts are buffered in a lock-free queue.
*   **Retry with Backoff**: Failed HTTP POSTs are retried **3 times** using an exponential backoff strategy (`1s -> 2s -> 4s`).
*   **CURL-based Engine**: Supports HTTPS, custom headers, and sub-second timeouts.

### 2.2 Noise Control (Suppression & Aggregation)
To manage high-frequency errors (e.g., CC bursts), the engine employs three layers of protection:

1.  **Alert Dependency Tree (Root-Cause Inhibition)**:
    -   **Level 1 (Sync Loss)**: If `TS_SYNC_LOSS` is firing, the engine automatically mutes `CC_ERROR`, `PCR_ERROR`, `TRANSPORT_ERROR`, `CRC_ERROR`, and all `TIMEOUT` (PAT/PMT/SDT) alerts.
    -   **Level 2 (Continuity Burst)**: If `CC_ERROR` is firing at high intensity, `PCR_JITTER` and `TSTD` alerts are suppressed, as packet loss inherently induces timing noise.
2.  **Wall-clock Timeout Evaluation**:
    -   Uses a background **Time-Wheel** to detect timeouts (e.g., PAT missing) even when the source stream has completely stopped (0 bps), ensuring alerts fire even without incoming packet triggers.
3.  **Sliding Window Aggregation**:
    -   Within a **10-second** window, identical alerts are suppressed and counted. At the end of the window, a summary message is sent (e.g., `CC_ERROR occurred 150 times`).

---

## 3. Standard Filter Masks (TR 101 290)

Alerts can be enabled or disabled per-stream using a bitmask aligned with ETSI standards:

| Index | Name | Bitmask |
| :--- | :--- | :--- |
| P1.1 | TS_sync_loss | `0x0001` |
| P1.3 | PAT_error | `0x0002` |
| P1.4 | CC_error | `0x0004` |
| P2.3 | PCR_error | `0x0040` |
| FAIL | Failover Event | `0x1000` |

---

## 4. Configuration Example

```nginx
alert {
    webhook_url http://ops-center.local/webhook;
    filter_mask 0xFFFF;
    cooldown    10s;
}
```
