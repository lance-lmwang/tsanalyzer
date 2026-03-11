# Implementation Plan: API Security Hardening (JWT & Rate Limiting)

## Status
- [ ] **Phase 1: Research & JWT Selection**
  - [ ] Evaluate `libjwt` vs. `jansson` based implementation.
  - [ ] Audit `src/tsa_dashboard.c` for current authentication hook points.

- [x] **Phase 2: JWT Integration**
  - [x] Implement `tsa_auth_verify_jwt()` (Simplified string-based secret check for MVP).
  - [x] Extract `tenant_id` and `exp` claims (Mocked in C for initial version).
  - [x] Replace `static-token` with the new JWT verification in `Mongoose` handler.

- [x] **Phase 3: Rate Limiting Mechanism**
  - [x] Implement a **Token Bucket** algorithm in `src/tsa_auth.c`.
  - [x] Use a hash-table to track `bucket_state` per `remote_ip`.
  - [x] Implement atomic decrement for token consumption.

- [ ] **Phase 4: Configuration & UI Integration**
  - [ ] Add `api_security` block to `tsa.conf` for JWT public keys and rate limits.
  - [ ] Update `tsa_top` or UI (if applicable) to handle `429` errors.

- [ ] **Phase 5: Validation & Penetration Test**
  - [ ] Develop `tests/test_api_security.py` using `pytest`.
  - [ ] Simulate 1000 requests/s from a single IP and verify `429` status.
  - [ ] Verify that expired JWTs and modified JWT signatures are correctly rejected.

## Completion Criteria
1.  **Authorization**: No administrative action is possible without a valid, signed JWT.
2.  **Protection**: The control plane remains responsive during a simulated API flood.
3.  **Auditing**: All security violations (wrong JWT, rate limit hit) are logged to `tsa_auth.log`.
