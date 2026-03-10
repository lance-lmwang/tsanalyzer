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

### Task 3.1: Migrate CC & Sync Loss [/]
- **Files**: `src/tsa_engine_tr101290.c`, `src/tsa_decode.c`, `src/tsa_alert.c`
- **Action**: Replace `tsa_debounce_t` calls with `tsa_alert_push_event()` (integrated via `tsa_alert_check_resolutions`).
- **Status**: IN PROGRESS. Initial integration for PAT/PMT/SDT/NIT timeouts implemented.

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
- **Task**: Standardize Webhook JSON payload. [ ]
- **Task**: Update CLI monitor to show "Active Alerts" list. [ ]
- **Validation**: COMPLETED. New metric `tsa_alert_status` added to `src/tsa_exporter_prom.c`.
