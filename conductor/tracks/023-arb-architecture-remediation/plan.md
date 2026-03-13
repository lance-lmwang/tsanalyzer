# Track Implementation Plan (023)

## Phase 1: Security Hardening (Lua Sandboxing)
- [ ] Task 1.1: In `src/tsa_lua.c`, remove `luaL_openlibs(lua->L)`.
- [ ] Task 1.2: Implement a whitelist of safe Lua libraries (e.g., `base`, `math`, `string`, `table`) using `luaL_requiref` or specific `luaopen_*` calls, ensuring `os` and `io` are excluded.

## Phase 2: Scalability (Lock-Free Packet Pool)
- [ ] Task 2.1: In `src/tsa_packet_pool.c`, remove `pthread_mutex_t lock` from `tsa_packet_pool_t`.
- [ ] Task 2.2: Rewrite `tsa_packet_pool_acquire` and `tsa_packet_unref` to use C11 `__atomic` compare-and-swap (CAS) operations for ring buffer head/tail management, or implement a thread-local caching mechanism to eliminate contention.

## Phase 3: I/O Performance (Eliminate Sleep)
- [ ] Task 3.1: Audit `src/tsa_source.c`, `src/tsp_main.c`, and other ingest-related files.
- [ ] Task 3.2: Replace `usleep()` calls in network polling loops with proper event-driven waiting (e.g., `poll`, `select`, or tuning socket timeouts) to prevent micro-stutters and throughput degradation.

## Phase 4: Defensive Parsing (PSI/SI Integrity)
- [ ] Task 4.1: Audit `src/tsa_psi.c` section reassembly and parsing loops. Implement strict `payload_len` vs `section_length` boundary assertions.
- [ ] Task 4.2: Audit `src/tsa_descriptors.c` to ensure descriptor lengths never cause read pointers to exceed the advertised containing loop bounds.

## Phase 5: Validation & Quality Gates
- [ ] Task 5.1: Build project (`make`).
- [ ] Task 5.2: Enforce code style (`make format && git diff --check`).
- [ ] Task 5.3: Execute comprehensive validation suite (`make full-test`).
- [ ] Task 5.4: Finalize and commit the ARB remediation changes.