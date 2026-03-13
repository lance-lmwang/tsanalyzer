# Track Implementation Plan (024)

## Phase 1: Security Hardening
- [ ] Task 1.1: Update `Dockerfile` to create a `tsanalyzer` group/user, change ownership of `/app`, and switch `USER tsanalyzer`.
- [ ] Task 1.2: Audit and patch `index.html` and `big_screen_noc.html` to replace `.innerHTML` with `.textContent` for rendering dynamic API data (e.g., `stream_id`, messages).
- [ ] Task 1.3: In `src/tsa_server_pro.c`, enforce `tsa_auth_verify_request` for the `/metrics` endpoint (or ensure it's structurally safe).

## Phase 2: Decoupling Control Plane
- [ ] Task 2.1: Audit `src/tsa_server_pro.c`'s `http_fn`. Reduce or remove the scope of `g_conn_lock` during metric string generation to prevent blocking the data plane.

## Phase 3: Webhook Resilience
- [ ] Task 3.1: Refactor `src/tsa_webhook.c` to prevent queue overflows from silently dropping alerts. Implement a basic aggregation strategy or reduce blocking `usleep` calls.

## Phase 4: Validation
- [ ] Task 4.1: Run `make format && git diff --check`.
- [ ] Task 4.2: Run `make full-test`.
- [ ] Task 4.3: Commit changes.