# Specification: API Security Hardening (JWT & Rate Limiting)

## 1. Objective
Strengthen the Control Plane's security by replacing static API tokens with JWT-based temporary authorization and protecting critical management endpoints from brute-force or high-frequency abuse through Rate Limiting.

## 2. Requirements
- **JWT (JSON Web Token)**: Implement HS256/RS256 signature verification for administrative POST/DELETE endpoints.
- **Expiration Policy**: Tokens MUST have an `exp` (expiration) claim; reject expired tokens.
- **Rate Limiting**: Protect endpoints from more than `N` requests per second per IP or Tenant.
- **Zero Block Hot Path**: The security check MUST occur in the Control Plane (Mongoose thread) and MUST NOT block the Metrology Reactor.

## 3. Architecture: Layered Defense
1.  **Transport**: TLS 1.3 encryption (mandatory for external API).
2.  **Authentication (JWT)**: Replaces current `Authorization: Bearer <static_token>`.
3.  **Authorization (Claims)**: Extract `tenant_id` and `role` (e.g., `admin`, `viewer`) from JWT.
4.  **Rate Limiting (Token Bucket)**:
    - Each IP/Tenant gets a `token_bucket_t` with a set `capacity` and `refill_rate`.
    - Drop requests with `429 Too Many Requests` if the bucket is empty.

## 4. Resource Constraints
- **Library**: Prefer a lightweight C JWT implementation (e.g., `libjwt` or embedded code) with minimal dependencies.
- **Latency**: JWT verification and rate-limit lookup MUST complete in < 1ms to maintain API responsiveness.
- **Memory**: The rate-limit tracker MUST use a fixed-size table to prevent memory exhaustion from "IP Spoofing" attacks.
