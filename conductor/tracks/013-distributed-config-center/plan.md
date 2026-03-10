# Implementation Plan: Distributed Configuration Center (Etcd/Consul)

## Status
- [ ] **Phase 1: Research & Library Integration**
  - [ ] Evaluate Etcd C/C++ clients (e.g., `etcd-cpp-apiv3` or a Mongoose-based REST/gRPC client).
  - [ ] Prototype a basic `PUT`/`GET` and `WATCH` on a local Etcd instance.

- [ ] **Phase 2: Configuration Watcher implementation**
  - [ ] Implement `src/tsa_config_watcher.c` as a background thread.
  - [ ] Implement a gRPC/HTTP/JSON-based event parser for incoming configuration updates.
  - [ ] Create a "State Reconciliation" logic to align local streams with the remote Etcd state.

- [ ] **Phase 3: Refactor Hot-Reload Engine**
  - [ ] Modify `src/tsa_conf.c` to accept configuration from memory buffers (currently file-based).
  - [ ] Implement the `Revision` check to avoid race conditions during rapid config updates.

- [ ] **Phase 4: Multi-Node HA Scenarios**
  - [ ] Implement a **Leader Election** mechanism (optional, for management tasks) using Etcd TTL leases.
  - [ ] Develop a mechanism for nodes to report their "Applied Revision" back to Etcd for centralized observability.

- [ ] **Phase 5: Validation & Performance Test**
  - [ ] Deploy 3 Analysis Probes + 1 Etcd cluster using Docker Compose.
  - [ ] Measure the time from "Etcd PUT" to "All 3 Probes Updated". Target: < 100ms.
  - [ ] Simulate network partition (disconnect one Probe) and verify that it maintains its state and reconciles upon reconnection.

## Completion Criteria
1.  **Uniformity**: 100% agreement between multiple Analysis Probes within the synchronization window.
2.  **Robustness**: System remains operational during configuration store downtime.
3.  **Observability**: Centralized dashboard can see the exact configuration revision running on each node.
