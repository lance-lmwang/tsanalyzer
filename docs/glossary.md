# TsAnalyzer: Unified Metrology Glossary

This glossary defines the key technical terms used across the TsAnalyzer library, bridging the gap between Traditional Broadcast Engineering and modern IT/Cloud Infrastructure.

---

### A
*   **AC (Accuracy Error)**: A component of 3D PCR Jitter representing the short-term deviation of the PCR value from its ideal position.
*   **AF (Adaptation Field)**: An optional field in the TS header that carries PCRs, splicing points, and private data.
*   **AU (Access Unit)**: A coded representation of a unit of information, such as a single video frame (I, P, or B).

### B
*   **BSM (Buffer Safety Margin)**: A TsAnalyzer-specific metric representing the percentage of decoder buffer remaining before an underflow/overflow occurs.

### C
*   **CC (Continuity Counter)**: A 4-bit field in the TS header used to detect packet loss or duplication per PID.
*   **CPB (Coded Picture Buffer)**: The buffer in the T-STD that holds the compressed video data before it is decoded.

### D
*   **DR (Drift Error)**: A component of 3D PCR Jitter representing the long-term frequency deviation of the encoder's 27MHz clock.
*   **DTS (Decoding Time Stamp)**: A 33-bit value indicating the exact moment an Access Unit must be removed from the buffer and decoded.

### E
*   **EB (Elementary Buffer)**: The final buffer in the T-STD simulation model before data enters the decoder.
*   **Entropy (Shannon)**: A measure of information density. TsAnalyzer uses entropy variance to distinguish between "live noise" and "frozen/black frames."

### G
*   **GOP (Group of Pictures)**: The sequence of I, P, and B frames. TsAnalyzer audits the stability of GOP structures to ensure decoder compatibility.

### H
*   **HAT (Hardware Arrival Time)**: The nanosecond-precision timestamp recorded by the NIC at the exact moment a packet is received.

### I
*   **IAT (Inter-Arrival Time)**: The temporal delta between two consecutive packets. Statistical distribution of IAT is used to detect network micro-bursts.

### M
*   **MDI (Media Delivery Index)**: Comprised of **Delay Factor (DF)** and **Media Loss Rate (MLR)**, as defined in RFC 4445.

### O
*   **OJ (Overall Jitter)**: The combined jitter value encompassing network-induced arrival noise and encoder-induced timing errors.

### P
*   **PCR (Program Clock Reference)**: The 27MHz master reference clock embedded in the TS to synchronize decoders.
*   **PID (Packet Identifier)**: A 13-bit field identifying the stream type (Video, Audio, Data, PSI).
*   **PTS (Presentation Time Stamp)**: A 33-bit value indicating when a decoded frame should be displayed to the viewer.

### R
*   **RST+ (Remaining Safe Time)**: A predictive metric calculating how many milliseconds/seconds a stream can survive at current network conditions before a decoder freeze.

### S
*   **SCTE-35**: The ANSI/SCTE standard for Digital Program Insertion (DPI) signaling (Ad-insertion).
*   **STC (System Time Clock)**: The local 27MHz reference clock reconstructed by TsAnalyzer's Software PLL.

### T
*   **T-STD (Transport System Target Decoder)**: The normative mathematical model of a decoder defined in ISO/IEC 13818-1.

### V
*   **VBV (Video Buffering Verifier)**: The buffer model for MPEG-2 video (equivalent to CPB in H.264/H.265).
*   **VSTC (Virtual System Time Clock)**: The 27MHz timeline used internally by TsAnalyzer to perform all metrology calculations.
