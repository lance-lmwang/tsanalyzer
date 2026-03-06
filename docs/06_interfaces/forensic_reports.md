# Interactive HTML Forensic Reports

To bridge the gap between bit-exact engineering data and high-level incident resolution, TsAnalyzer generates standalone, interactive HTML forensic reports.

---

## 1. Concept: The "Portable Audit Trail"

When a critical incident is archived, the appliance can export a self-contained `.html` file that includes all relevant telemetry and visual evidence without requiring a connection to the TsAnalyzer server.

### 1.1 Report Components
*   **Executive Summary**: Health Score, SLA impact, and RCA attribution (Network vs. Encoder).
*   **Synchronized Charts**: Interactive JS-based charts (via Plotly or D3) showing PCR Jitter, Bitrate, and MDI-DF aligned to the millisecond.
*   **NALU Trail**: A list of all video frames around the point of failure, highlighting GOP gaps or malformed headers.
*   **Visual Snapshot**: The sparse thumbnail of the nearest IDR frame.
*   **Hex Inspector**: A bit-level view of the problematic TS packets (e.g., the packet that triggered a CRC error).

---

## 2. Generation Engine

The report is generated using a two-stage process:
1.  **JSON Export**: The Metrology Engine flushes a complete incident state to a temporary JSON file.
2.  **Template Binding**: A Python-based generator (`scripts/gen_forensic_report.py`) binds the JSON data to an optimized HTML5/JS template (`reports/template.html`).

---

## 3. Operational Value

*   **SLA Dispute Resolution**: Unambiguous, portable proof of failure origin to share with vendors or CDN providers.
*   **NOC Training**: Reviewing past incidents in a visual, easy-to-understand format.
*   **Offline Investigation**: Analyzing incidents on air-gapped forensic workstations.
