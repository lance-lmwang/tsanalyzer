# Specification: High Availability (HA) & Stateful Failover

## 1. Objective
Ensure that in the event of hardware failure, a secondary TsAnalyzer node can take over monitoring tasks without losing critical runtime state (e.g., alert firing duration, cumulative error counts, PCR history).

## 2. Requirements
- **Cluster Heartbeat**: Use Etcd Leases to maintain node presence. If a lease expires, a "Node Down" event is triggered.
- **State Checkpointing**: Periodically (e.g., every 5-10s) save the binary runtime state of critical stream metrics to the distributed KV store.
- **Leader Election (per Stream)**: Ensure that only one node is the active monitor for a specific `stream_id` to prevent duplicated reporting.
- **Zero-Drop Handover**: The standby node should initialize its analyzers using the last-known-good checkpoint.

## 3. Architecture: Checkpoint & Resume
1.  **Serialization**: Convert `tsa_stream_t`'s metrology state (e.g., `cc_error_total`, `alert_sm_state`) into a compact byte array.
2.  **Persistence**: The Active Node writes this to `/tsa/runtime/{stream_id}/checkpoint`.
3.  **Failover Trigger**: Cluster manager detects node failure and re-assigns the `stream_id` to a healthy node.
4.  **Resumption**: The New Node reads the `/tsa/runtime/{stream_id}/checkpoint` before starting the metrology reactor.

## 4. Resource Constraints
- **Performance**: Checkpointing MUST occur in the background and SHOULD be throttled to avoid saturating the Control Plane network.
- **Data Size**: Each checkpoint should be small (< 4KB) to ensure sub-millisecond read/write latency in Etcd.
