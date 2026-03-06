# Technical Spec: KPI Aggregation Layer (Layer 6)

The KPI Aggregation Layer (Layer 6) abstracts the high-resolution engineering metrology from Layer 3, the historical data from Layer 5, and the alarm states from Layer 4 into actionable business intelligence.

---

## 1. Core KPIs Definitions

TsAnalyzer focuses on four primary KPIs designed for NOC (Network Operations Center) triage.

### 1.1 Stream Risk Level (Real-time Triage)
- **Goal**: Identify streams most likely to fail in the next 60 seconds.
- **Inputs**:
    - `ACTIVE_Critical_Alarm_Count` (Layer 4)
    - `Remaining_Safe_Time_Net` (RST-N from Layer 3)
    - `Current_MDI_MLR` (Layer 3)
- **Logic**:
    - **CRITICAL (Red)**: Any active P1 error OR RST < 5s OR MLR > 0.
    - **HIGH (Orange)**: RST < 15s OR active P2 error.
    - **NORMAL (Green)**: All conditions stable.

### 1.2 Compliance Grade (SLA Compliance)
- **Goal**: Grade the stream based on historical uptime.
- **Inputs**: `SLA_Availability_Pct` (24h window from Layer 5).
- **Grading Scale**:
    - **Grade A**: Availability $\ge$ 99.99%
    - **Grade B**: 99.9% $\le$ Availability < 99.99%
    - **Grade C**: 99.0% $\le$ Availability < 99.9%
    - **Grade F**: Availability < 99.0%

### 1.3 Stability Index (Long-term Health)
- **Goal**: Quantify the "noise" or "turbulence" of the link, even if no errors are firing.
- **Inputs**:
    - `Avg_MDI_DF` (Rolling 60s from Layer 5)
    - `Max_PCR_Jitter` (Rolling 60s from Layer 5)
    - `SRT_RTT_Variance` (Layer 3)
- **Score (0-100)**: A weighted index where lower jitter and stable RTT result in a higher score.
    - **Formula (Example)**: $100 - (Jitter_{penalty} + Jitter_{MDI} + RTT_{var\_penalty})$

### 1.4 Encoder Integrity Score
- **Goal**: Isolate the health of the source (encoder) from the transport network.
- **Inputs**:
    - `PCR_Drift_ppm` (Layer 3)
    - `T_STD_Buffer_Occupancy` (Layer 3)
- **Logic**: High occupancy or high clock drift reduces the score, indicating potential encoder instability before it results in a CC or Sync error.

---

## 2. Aggregation Logic & Frequency

KPIs are updated at a lower frequency than raw metrology to ensure UI stability.
- **Update Frequency**: Every 1.0 second (1Hz).
- **Hysteresis**: KPI downgrades (e.g., Green to Red) are **Instant**. KPI upgrades (e.g., Red to Green) require a **5-second stable state** to prevent visual flickering on the dashboard.

---

## 3. Data Schema (`tsa_kpi_t`)

```json
{
  "stream_id": "STR-HD-01",
  "timestamp": "2026-02-28T09:00:00Z",
  "kpis": {
    "risk_level": "CRITICAL",
    "compliance_grade": "A",
    "stability_index": 85,
    "encoder_integrity": 92
  },
  "primary_drivers": [
    "Active P1.1 (Sync Loss)",
    "RST-N falling below 10s"
  ]
}
```

---

## 4. NOC Dashboard Integration (Layer 7)

Layer 6 provides the "Sorting Key" for Layer 7's fleet view. The dashboard sorts streams based on:
1.  **Risk Level** (Critical first).
2.  **Compliance Grade** (Lowest grade first).
3.  **Stability Index** (Most turbulent first).

This ensures that the most problematic streams always "float" to the top of the monitor wall, matching the Sencore VideoBRIDGE operational philosophy.
