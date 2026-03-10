# Initial Concept
TsAnalyzer is a deterministic Transport Stream metrology platform for professional broadcast-grade analysis.

# Product Vision
TsAnalyzer is a forensic-grade **Software-Defined Measurement Instrument** designed for absolute precision in TS metrology. It aims to provide the "Source of Truth" for protocol consistency, timing accuracy, and buffer modeling.

# Target Users
- Broadcast Engineers requiring laboratory-grade analysis.
- SREs managing high-availability video networks.
- Developers building mission-critical transport protocols.
- Legal and compliance teams needing forensic-grade evidence bundles.

# Key Features
1. **Deterministic Execution**: 100% bit-identical results across PCAP replays (MD5-consistent JSON).
2. **Temporal Physics Engine**: Sub-microsecond arrival precision via NIC Hardware Timestamping.
3. **Annex D Buffer Modeling**: ISO/IEC 13818-1 faithful simulation (VBV/T-STD) to predict decoder overflows.
4. **Metrology Causality Engine**: Advanced TR 101 290 P1/P2/P3 analysis and RST (Remaining Safe Time) derivation.
5. **High-Performance Architecture**: Industrial-grade 1.2M PPS engine with zero-allocation plugin architecture, optimized for NUMA and cache efficiency.

# Design Principles
- **Accuracy First**: Correctness is the only requirement. If results are not bit-exact, they are wrong.
- **Reproducibility**: Any analysis result must be perfectly reproducible from the original packet capture.
- **Traceability**: All measurements must trace back to the physical layer timing or protocol standards.
- **Performance**: High throughput must never compromise metrological integrity.
