# PCR Bitrate Metrology Standards (ISO/IEC 13818-1)

## 1. Core Calculation Formula
According to the ISO/IEC 13818-1 specification, the Transport Stream bitrate $R_{ts}$ must be strictly calculated based on all physical bytes transmitted between PCR observation points:

$$R_{ts} = \frac{\Delta Packets \times 188 \times 8 \times 27,000,000}{\Delta PCR (ticks)}$$

## 2. Null Packet Handling (PID 0x1FFF)
**Critical Implementation Detail**: To ensure the calculated bitrate represents the true physical line speed (L2), Null packets (PID 0x1FFF) MUST NOT be filtered before the bitrate calculation point. 

- The `total_ts_packets` counter must be incremented for every valid 188-byte packet entering the engine.
- PCR metrology engines must use this global counter to capture the full interval span, even if they do not process the content of Null packets.

## 3. Unit Definitions and Discrepancies
Bitrate units can be ambiguous across different tools:
- **Metric Standard (Used by TsAnalyzer)**: $1 \text{ Mbps} = 1,000,000 \text{ bps}$.
- **Binary Units (Used by some packet generators)**: $1 \text{ Mbps} = 1,024 \times 1024 = 1,048,576 \text{ bps}$.

If TsAnalyzer reports ~10.48 Mbps for a stream configured as "10 Mbps" in a generator, it likely means the generator is using binary units. TsAnalyzer strictly adheres to decimal units ($10^6$) to maintain consistency with ITU standards.

## 4. Precision and Smoothing
- Use `double` precision for all intermediate steps in the ISO formula to avoid rounding errors.
- The default EMA (Exponential Moving Average) smoothing factor is set to `0.1` to balance responsiveness and stability.
