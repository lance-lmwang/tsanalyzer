# Track: Production Readiness & Control Plane Hardening (024)

## 1. Goal
Address the Google/Amazon ARB Production Readiness Review (PRR) blockers. Ensure the application is secure, scalable, and resilient enough for public internet or enterprise core-network deployments.

## 2. Rationale
The ARB identified critical launch blockers in the control plane and UI:
- **Security**: The Docker container runs as root. The web UI is vulnerable to DOM-based XSS via `innerHTML`. The `/metrics` endpoint is unauthenticated.
- **Scalability**: The `g_conn_lock` blocks the core engine when Prometheus scrapes metrics.
- **Reliability**: The Webhook queue drops critical alerts during network stutters because it relies on synchronous sleeps for retries.

## 3. Scope
- `Dockerfile` (Non-root user)
- `index.html`, `big_screen_noc.html` (XSS Remediation)
- `src/tsa_server_pro.c` (API Authentication and Lock removal)
- `src/tsa_webhook.c` (Alert aggregation and async fixes)

## 4. Acceptance Criteria
- Dockerfile creates and uses a `nonroot` user.
- No instances of `.innerHTML` used for dynamic data in HTML files.
- `/metrics` endpoint is protected or the locking mechanism is decoupled.
- Webhook engine no longer uses `usleep` during send loops and implements basic queue overflow protection.
- All tests pass (`make full-test`).