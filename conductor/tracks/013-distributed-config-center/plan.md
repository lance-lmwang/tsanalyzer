# Implementation Plan: Distributed Configuration Center (Inotify & Remote)

## Status
- [x] **Phase 1: Local Configuration Watcher**
  - [x] Implement: `src/tsa_conf_watcher.c` using `inotify` to detect file changes.
  - [x] Implement: Auto-reload of `tsa_full_conf_t` on modification.
  - [x] Validate: Background thread correctly re-parses config without engine restart.

- [ ] **Phase 2: Configuration Reconciliation Engine**
  - [ ] Implement: Logic to compare old vs new conf and dynamically start/stop streams.
  - [ ] Implement: Update global settings (thread counts, ports) on the fly where possible.

- [ ] **Phase 3: Remote Config Center (Etcd/Consul)**
  - [ ] Evaluate Etcd C/C++ clients.
  - [ ] Implement remote configuration provider.

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
