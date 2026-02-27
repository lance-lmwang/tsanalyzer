# API Reference: Smart Gateway Automation

The suite provides a dual-layer API: a high-performance **C Library** for core logic and a **REST/gRPC Automation API** for fleet orchestration.

---

## 1. Automation API (REST/gRPC)

### 1.1 `POST /api/v1/streams`
Registers a monitoring and relay task.
- **Payload Parameters**:
    - `tenant_id`: Mandatory for SaaS-grade isolation.
    - `visibility_mode`: `BLIND` (SRT Only), `SEMI-BLIND` (P1/P2), or `FULL` (ABR Audit).
    - `pacing_enabled`: Enables TsPacer repair before relay.
    - `fail_strategy`: `transparent_pass_through` (L4 Bypass) or `block`.

### 1.2 `GET /api/v1/streams/{id}`
Retrieves atomic snapshots of quantifiable metrics (RST, MDI, T-STD) and SLA performance.

---

## 2. Core C Library API

**Library Prefixes**: `tsa_` (Analyzer), `tsp_` (Pacer)

### 2.1 `tsa_handle_t* tsa_create(const tsa_config_t* cfg)`
Initializes the Brain.
- **Profile Support**: `TSA_PROFILE_STRICT_SYNC` or `TSA_PROFILE_RESILIENT_WAN`.
- **Memory Contract**: Pre-allocates zero-copy pools. No Edition-based limits; visibility is controlled by stream keys.

### 2.2 `tsa_status_t tsa_process_packet(tsa_handle_t* h, const uint8_t* pkt, uint64_t now)`
Wait-free analysis entry point. Supports automated MPTS/SPTS re-sync.

### 2.3 `tsp_handle_t* tsp_create(const tsp_config_t* cfg)`
Initializes the Muscle for pacing. Integrated with the server egress dispatcher.

---

## 3. Engineering Implementation Contract

1. **Wait-Free SPSC**: All thread handoffs MUST be wait-free.
2. **Watchdog Integrity**: The server MUST monitor analysis thread heartbeats to trigger L4 Bypass in < 10ms.
3. **128-bit Monotonicity**: Mandatory for all timing deltas using `int128_t` nanoseconds.
