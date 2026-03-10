# Specification: Distributed Configuration Center (Etcd/Consul)

## 1. Objective
Enable real-time, consistent configuration distribution across multiple TsAnalyzer nodes (Active-Active) to ensure that all probes in a cluster have a unified view of stream monitoring tasks.

## 2. Requirements
- **Consistency**: All nodes must eventually converge to the same configuration state defined in the centralized store.
- **Real-time Synchronization**: Configuration changes made via the Control Plane should be pushed to all Analysis Probes in sub-second time.
- **Resilience (Shadow Cache)**: Probes must maintain a local copy of the last-known-good configuration to survive temporary network partitions or configuration store downtime.
- **Watch Mechanism**: Use a non-polling, event-driven mechanism (e.g., Etcd gRPC Watch or Consul HTTP Long Polling) to minimize control plane overhead.

## 3. Architecture: The Watcher Thread
A dedicated `tsa_config_watcher` thread will manage the lifecycle of the distributed configuration:
1.  **Bootstrap**: On startup, fetch the full configuration tree from `/tsa/config/` and apply it locally.
2.  **Watch**: Subscribe to changes on the `/tsa/config/` prefix.
3.  **Atomic Swap**: When a change is detected, trigger a memory-based hot-reload (using the existing two-phase commit logic in `tsa_conf.c`).

## 4. Conflict & Multi-Tenancy
- **Revision Tracking**: Each configuration update includes a `revision` number. Probes will ignore updates with a lower revision than the current local state.
- **Global Constraints**: Use Etcd transactions (TXN) to ensure that a `stream_id` is unique across the entire cluster, preventing dual-monitoring of the same multicast source unless explicitly intended for HA.
