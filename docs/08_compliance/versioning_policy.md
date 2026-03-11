# Versioning Policy

TsAnalyzer Pro adheres to **Semantic Versioning 2.0.0 (SemVer)** to ensure predictable integration for enterprise customers.

---

## 1. Version Format: `MAJOR.MINOR.PATCH`

1.  **MAJOR**: Incompatible API changes (e.g., changing the JSON snapshot schema).
2.  **MINOR**: New functionality in a backwards-compatible manner (e.g., adding a new TR 101 290 check).
3.  **PATCH**: Backwards-compatible bug fixes.

## 2. API Stability

*   **REST API v1**: Guaranteed stable. Any breaking changes will trigger an increment to `v2` and a 6-month deprecation period.
*   **Lua SDK**: Backward compatibility is maintained for core `tsa.*` objects across minor versions.

## 3. Deprecation Cycle

When a feature is marked for deprecation:
1.  It will be flagged in the **CHANGELOG**.
2.  A runtime warning will be emitted in the logs.
3.  The feature will be removed in the next **MAJOR** release.
