# Unified Alert & Debounce Engine - Implementation Plan

## Phase 1: Core Engine Implementation
**Goal**: Create a generic alert tracking engine.

### Task 1.1: Implement `tsa_alert_tracker_t` [x]
- **Files**: `include/tsa_alert.h`, `src/tsa_alert.c`
- **Action**: Implement state transition logic: `IDLE -> PENDING -> ACTIVE -> RECOVERING`.
- **Validation**: COMPLETED. `tsa_alert_update` handles status transitions.

---

## Phase 2: Configuration & Definition Matrix
**Goal**: Define alert thresholds.

### Task 2.1: Implement Alert Definition Matrix [x]
- **Files**: `src/tsa_conf.c`, `include/tsa_alert.h`, `include/tsa_conf.h`
- **Action**: Load alert thresholds (raise/fall) from `tsa_config_t`.
- **Validation**: COMPLETED. Filter mask and config-driven logic implemented.

---

## Phase 3: Business Logic Integration
**Goal**: Migrate existing hardcoded alerts to the new engine.

### Task 3.1: Migrate CC & Sync Loss [x]
- **Files**: `src/tsa_engine_tr101290.c`, `src/tsa_decode.c`, `src/tsa_alert.c`
- **Action**: Replace `tsa_debounce_t` calls with `tsa_alert_update()` (integrated via `tsa_alert_check_resolutions`).
- **Validation**: COMPLETED. TR 101 290 core checks now use the unified engine with suppression logic. Verified via `test_health_debounce`.

---

## Phase 4: Alert Suppression & Correlation [x]
**Goal**: Reduce alert noise (storm control).

### Task 4.1: Hierarchy Suppression [x]
- **Files**: `src/tsa_alert.c`
- **Action**: Implement logic: "If SYNC_LOSS is active, suppress all other alerts EXCEPT SYNC itself."
- **Validation**: COMPLETED. Verified via `tsa_alert_update` logic and snapshot integration.

---

## Phase 5: Exporter & API Update [x]
- **Task**: Update Prometheus exporter to use the new alert status. [x]
- **Task**: Standardize Webhook JSON payload. [x]
- **Task**: Update CLI monitor to show "Active Alerts" list. [x]
- **Validation**: COMPLETED. New metric `tsa_alert_status` added to Exporter and real-time display added to `tsa_top`.
