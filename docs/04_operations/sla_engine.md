# SLA Engine & Compliance

The SLA Engine aggregates incidents into long-term business metrics.

## 1. Availability Calculation

Availability is defined as the percentage of time the service is free of **ACTIVE** Priority 1 faults.
$$Availability \% = \frac{T_{window} - \sum T_{P1\_Incidents}}{T_{window}} \times 100$$

## 2. Compliance Grading (The Sencore Model)

Streams are graded based on their 24-hour SLA performance:
*   **Grade A**: 99.99% - 100% (Nominal)
*   **Grade B**: 99.9% - 99.99% (Slight Impairment)
*   **Grade C**: 99.0% - 99.9% (Degraded)
*   **Grade F**: < 99.0% (Critical Failure)

## 3. Incident History Persistence

Layer 5 maintains a rolling history of logical incidents (Merged events) rather than raw alarms. This allows for:
*   **Trend Analysis**: Detecting recurring patterns in packet loss or jitter.
*   **Audit Trails**: Providing bit-exact evidence (timestamps and byte-offsets) for SLA dispute resolution.
