# Track: ARB Architecture Remediation (023)

## 1. Goal
Address the critical findings identified during the Architecture Review Board (ARB) assessment. The focus is on hardening security, eliminating severe scalability bottlenecks, and improving the resilience of the ingest and parsing layers to meet industrial-grade and 100Gbps readiness standards.

## 2. Rationale
The ARB highlighted several systemic risks that prevent the system from safely and efficiently scaling:
- **Security**: The current Lua implementation exposes `os` and `io` standard libraries, posing a critical Remote Code Execution (RCE) risk.
- **Scalability**: The central packet pool uses a single `pthread_mutex_t`, causing immense lock contention on multicore systems under heavy load. The ingest threads use `usleep`, destroying network throughput predictability.
- **Resilience**: The PSI/SI parsers lack strict bounds-checking against malicious or corrupted payload lengths.

## 3. Scope
- `src/tsa_lua.c` (Lua sandboxing)
- `src/tsa_packet_pool.c` (Memory pool lock contention)
- `src/tsa_source.c`, `src/tsg_main.c`, `src/tsp_main.c`, etc. (Eliminate `usleep` in hot paths)
- `src/tsa_psi.c`, `src/tsa_descriptors.c` (Defensive programming and boundary checks)

## 4. Acceptance Criteria
- Lua scripts can no longer access `os` or `io` libraries (safe sandboxing achieved).
- `tsa_packet_pool` operations use lock-free semantics (`__atomic` or similar) or TLS, removing the central `pthread_mutex_t`.
- Zero instances of `usleep` in ingest polling or tight network loops.
- PSI/SI parsing includes strict bounds verification before pointer arithmetic or length-based loops.
- All 146+ unit and integration tests continue to pass (`make full-test`).