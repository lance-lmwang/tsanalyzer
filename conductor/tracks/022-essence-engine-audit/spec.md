# Track: Essence Engine Audit & Refactor (022)

## 1. Goal
Address critical findings from the deep code audit of `src/tsa_engine_essence.c`. Specifically:
1. Eliminate floating-point operations from the TS packet hot path (`tsa_tstd_update_leak`).
2. Fix potential null pointer dereferences when operating in file-based (offline) analysis mode.
3. Add robust bounds checking for PID array accesses.
4. Prevent event storms on T-STD Underflow by adding debounce logic.
5. Remove magic numbers and establish explicit macros for defaults and thresholds.

## 2. Rationale
`tsa_engine_essence.c` sits in the highest frequency execution path (hot path) of the TS analyzer. The presence of floating-point arithmetic reduces overall throughput, while missing null pointer checks will immediately crash the application when analyzing local files. Addressing these issues significantly enhances both stability and performance.

## 3. Scope
- `src/tsa_engine_essence.c`
- `include/tsa_internal.h` (if needed for `last_underflow_dts` addition to T-STD struct)

## 4. Acceptance Criteria
- Code compiles without warnings (`make`).
- Unit and integration tests pass successfully (`make test`).
- Zero floating-point arithmetic in `tsa_tstd_update_leak`.
- Offline TS file analysis does not trigger segmentation faults.
- Multiple packets belonging to the same delayed AU trigger exactly one Underflow alert.
