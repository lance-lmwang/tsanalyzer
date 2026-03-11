# Security Policy

TsAnalyzer Pro is designed for mission-critical broadcast environments. Security is a fundamental pillar of our development lifecycle.

---

## 1. Reporting a Vulnerability

We value the work of security researchers. If you find a security vulnerability in TsAnalyzer, please report it via one of the following channels:

*   **Email**: security@tsanalyzer.pro
*   **Encrypted**: GPG Key [0xDEADC0DE]

Please **do not** open a public issue for security vulnerabilities until we have had a chance to remediate the issue.

## 2. Response Timeframe

*   **Initial Acknowledgment**: 24 hours.
*   **Triage & Severity Assignment**: 72 hours.
*   **Fix Availability (Critical)**: Within 7 business days.

## 3. Security Hardening Measures

### 3.1 Binary Protections
The TsAnalyzer binary is compiled with modern exploit mitigations:
- **ASLR**: Address Space Layout Randomization.
- **NX**: No-eXecute stacks.
- **Stack Canaries**: Protection against buffer overflows.

### 3.2 Network Security
- **JWT Auth**: Mandatory for all REST API access.
- **Rate Limiting**: Protection against API flooding.
- **Sandbox**: Embedded Lua engine is strictly sandboxed (No OS/IO access).
