# Track Implementation Plan (022)

## Phase 1: Struct Updates
- [ ] Task 1.1: Add `uint64_t last_underflow_dts;` to `tsa_tstd_status_t` within `include/tsa_internal.h` (or similar location depending on where T-STD status is defined).

## Phase 2: Core Refactoring
- [ ] Task 2.1: In `src/tsa_engine_essence.c`, define constants `TSA_NANOS_PER_SEC`, `TSA_DEFAULT_RX_BPS`, and `TSA_DEFAULT_RBX_BPS`.
- [ ] Task 2.2: Refactor `tsa_tstd_update_leak()` to remove floating-point operations. Apply shift and division over `TSA_NANOS_PER_SEC`. Remove hardcoded numbers.
- [ ] Task 2.3: Fix `h->live` null pointer accesses within `tsa_tstd_update_leak()` and `essence_on_ts()`. Add bounds checking for `pid` in `essence_on_ts()`.
- [ ] Task 2.4: Introduce debounce logic to `essence_on_ts()` to suppress repeated Underflow events triggered by the same Access Unit.

## Phase 3: Validation and Submission
- [ ] Task 3.1: Compile source code (`make`).
- [ ] Task 3.2: Run formatting checks (`make format`).
- [ ] Task 3.3: Run tests and ensure successful execution (`make test`).
- [ ] Task 3.4: Validate there are no whitespace issues via `git diff --check`.
- [ ] Task 3.5: Commit with a summary of the performed fixes.
