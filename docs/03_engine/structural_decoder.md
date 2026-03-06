# Structural Decoder & Sparse Inspection

Layer 2 performs protocol decomposition and content-layer sniffing without the overhead of full decoding.

## 1. Fast PSI/SI Parsing

The engine parses the full DVB/MPEG table set (PAT, PMT, NIT, SDT, CAT, EIT).
*   **Recursive Decomposition**: Tables are reassembled from sections across multiple packets.
*   **Version Tracking**: To save CPU, the engine uses a fast-path version check. If the `version_number` remains unchanged, the engine skips redundant parsing and CRC32 checks.

## 2. Zero-Copy NALU Sniffer

For H.264 and HEVC, the engine implements a sparse scanner that operates directly on the PES payload within the ring buffer.
*   **Capabilities**:
    *   Identifies Frame Types (I, P, B).
    *   Extracts GOP structure and cadence.
    *   Parses SPS for resolution, profile, and level.
*   **Efficiency**: Uses a state-machine start-code scanner (`0x000001`) that requires no memory allocations or buffer copies.

## 3. SCTE-35 & Metadata Audit

Decodes Digital Program Insertion markers and calculates the absolute PTS alignment error between the splice command and the actual video IDR frame.
