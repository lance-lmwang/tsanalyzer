# Implementation Plan: High Availability (HA) & Stateful Failover

## Status
- [ ] **Phase 1: Binary State Serialization**
  - [ ] Identify critical fields in `ts_stream_t` for state recovery (e.g., alert state, PID counts, PCR PLL parameters).
  - [ ] Implement `tsa_stream_save_state()` and `tsa_stream_restore_state()` functions.

- [ ] **Phase 2: Cluster Heartbeat & Presence**
  - [ ] Implement Etcd-based **Node Lease** mechanism.
  - [ ] Create a "Node Status Monitor" thread that watches for lease expirations.

- [ ] **Phase 3: Periodic Checkpointing**
  - [ ] Integrate checkpointing into the metrology loop (low-frequency task).
  - [ ] Implement throttled writes to Etcd (e.g., only update if state changed significantly or after 10s).

- [ ] **Phase 4: Failover Logic Implementation**
  - [ ] Develop the "Task Re-assignment" algorithm for the Cluster Manager.
  - [ ] Modify the stream startup logic to pull and apply the last checkpoint.

- [ ] **Phase 5: Failover Stress Test**
  - [ ] Setup a 3-node cluster and simulate a `kill -9` on one node.
  - [ ] Measure the time for another node to take over and verify that the "CC Error Count" or "Alert Duration" is maintained correctly.

## Completion Criteria
1.  **Continuity**: Alert firing durations do not reset when a node fails and is replaced.
2.  **Reliability**: No duplicate monitoring of the same `stream_id` (single-node ownership enforced).
3.  **Efficiency**: Checkpointing overhead is < 1% of the total CPU usage.
