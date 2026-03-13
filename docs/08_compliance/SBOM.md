# Software Bill of Materials (SBOM)

TsAnalyzer Pro is committed to supply chain transparency and open-source compliance. This document lists all third-party components included in the build.

---

## 1. Direct Dependencies

| Component | Version | License | Role |
| :--- | :--- | :--- | :--- |
| **Haivision SRT** | v1.5.x | MPL 2.0 | Reliable UDP transport layer. |
| **Libcurl** | v8.x | MIT/X derivative | HLS chunk ingestion & Webhooks. |
| **Libpcap** | v1.10.x | BSD-3-Clause | Physical packet capture (Passive). |
| **Lua JIT** | v2.1 | MIT | Dynamic pipeline scripting engine. |
| **Mongoose** | v7.x | GPLv2 / Commercial | Embedded REST API & WebSocket server. |
| **Zlib** | v1.2.x | Zlib | Compression for logs and HLS chunks. |

---

## 2. License Compliance

### 2.1 Copyleft Notice
TsAnalyzer core is linked against `libsrt` (MPL 2.0) and `mongoose` (GPLv2). If you distribute TsAnalyzer as a binary, you must ensure compliance with these licenses.

### 2.2 Redistribution
For enterprise customers requiring a proprietary license without GPL/MPL obligations, please contact the licensing team for the **TsAnalyzer Commercial SDK** variant.

---

## 3. Supply Chain Security
All dependencies are fetched from official sources and verified via SHA-256 checksums during the `build_deps.sh` phase to prevent dependency hijacking.
