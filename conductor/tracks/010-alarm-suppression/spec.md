# Specification: Alarm Flapping Suppression

## 1. Objective
To reduce the overhead and noise in the NOC (Network Operations Center) by localizing and aggregating high-frequency intermittent alerts (flapping) before they are dispatched via the Signaling Engine (Webhooks).

## 2. Technical Requirements
- **Aggregation Window**: A configurable time window (default: 10 seconds) for grouping identical alert types.
- **Flapping Detection**: Identify alerts that toggle between `FIRING` and `RESOLVED` states within the window.
- **Summary Notification**: After the window expires, send a single summary alert if more than N toggles occurred.
- **Zero-Latency First Hit**: The *first* occurrence of an alert MUST be dispatched immediately (no delay) to ensure sub-second response times. Subsequent hits within the window are aggregated.

## 3. Algorithm: Sliding State Machine
An alert entry for a specific `{stream_id, alert_type, pid}` will transition through:

1.  **IDLE**: No active alert.
2.  **ACTIVE (Instant Dispatch)**:
    - First hit: Push to `signal_queue` immediately.
    - Start `aggregation_timer`.
    - Set `hit_count = 1`.
3.  **SUPPRESSED**:
    - Subsequent hits within `aggregation_timer`: Increment `hit_count`.
    - Do NOT push to `signal_queue`.
4.  **SUMMARY (Delayed Dispatch)**:
    - On `aggregation_timer` expiry:
    - If `hit_count > 1`: Push a summary alert: `[Summary] Stream X had Y occurrences of Alert Z in the last 10s`.
    - Reset to **IDLE**.

## 4. Resource Constraints
- **Memory**: Use a pre-allocated fixed-size hash table (e.g., 4096 entries) to avoid `malloc` during the data plane analysis.
- **CPU**: The lookup must be $O(1)$ and thread-safe (lock-free or fine-grained spinlocks) since it sits in the metrology path.
