# Exporter Performance Optimization (4-Tier)

## Objective
Optimize the CPU utilization and memory access patterns of the metrics export path (`tsa_exporter_prom.c` and related API endpoints). The goal is to eliminate run-time memory allocations, reduce lock contention during snapshot generation, and provide tiered endpoints to prevent Prometheus from unnecessarily scraping massive PID detail payloads.

## Key Goals
1.  **Zero Allocation**: Remove `malloc` in `tsa_exporter_prom_v2` to prevent heap contention and fragmentation.
2.  **Zero-Copy Snapshots**: Transition from a SeqLock `memcpy` model to an atomic Ping-Pong double buffer model.
3.  **Endpoint Splitting**: Separate core system metrics from verbose PID metrics to reduce the HTTP response size by 90% for high-frequency scraping.
4.  **String Optimization**: Reduce CPU cycles spent in `snprintf` by pre-baking label strings.

## Constraints
- Must not break existing Prometheus dashboards (Grafana panels).
- Must retain backward compatibility for the `/metrics` endpoint if required by older configurations.
- The Ping-Pong buffer must correctly handle edge cases where a slow reader might observe an overwrite (though in our polling model, this is unlikely if readers are fast, or we might need triple buffering if strict isolation is required. For now, standard double buffering with atomic swap is sufficient for 1-second cadence).
