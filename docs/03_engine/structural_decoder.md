# Structural Decoder & Sparse Inspection

Layer 2 performs protocol decomposition and content-layer sniffing without the overhead of full decoding.

## 1. Multi-Standard SI/PSI Engine

Layer 2 implements a generic table decomposition framework capable of handling DVB, ATSC, and ISDB standards.

*   **Recursive Decomposition**: Tables are reassembled from sections across multiple packets.
*   **Plug-and-Play Descriptors**: Utilizes a metadata registry to parse descriptors based on the detected standard (e.g., handling ATSC AC-3 descriptors vs. DVB AAC descriptors).
*   **Version Tracking**: To save CPU, the engine uses a fast-path version check. If the `version_number` remains unchanged, the engine skips redundant parsing and CRC32 checks.

### 1.1 Support Matrix
| Standard | Critical Tables | Status |
| :--- | :--- | :--- |
| **DVB** | PAT, PMT, SDT, NIT, EIT, CAT | Full Support |
| **ATSC** | PAT, PMT, MGT, TVCT, CVCT | Planned |
| **ISDB** | PAT, PMT, NIT, SDT (ARIB) | Planned |

---

## 2. Branchless TS Parser (Fast Path)

To achieve 10 Gbps (8.3M PPS), the TS packet header parser must execute in ≤ 40 CPU cycles. v3 utilizes branchless logic to avoid pipeline stalls.

### 2.1 Implementation Pattern
```c
static inline void ts_parse_header(const uint8_t *pkt, ts_header_t *h) {
    h->sync = pkt[0];
    h->pid  = ((pkt[1] & 0x1F) << 8) | pkt[2];
    h->pusi = (pkt[1] >> 6) & 1;
    h->afc  = (pkt[3] >> 4) & 3;
    h->cc   = pkt[3] & 0xF;
}
```

### 2.2 Continuity Tracking
The CC tracker uses branch-free arithmetic to increment error counters:
```c
void cc_update(cc_tracker_t *cc, uint8_t new_cc) {
    uint8_t expected = (cc->last_cc + 1) & 0xF;
    uint8_t err = (new_cc != expected);
    cc->errors += err;
    cc->last_cc = new_cc;
}
```

---

## 3. Zero-Copy NALU Sniffer
For H.264 and HEVC, the engine implements a sparse scanner that operates directly on the PES payload within the ring buffer.
*   **Capabilities**:
    *   Identifies Frame Types (I, P, B).
    *   Extracts GOP structure and cadence.
    *   Parses SPS for resolution, profile, and level.
*   **Efficiency**: Uses a state-machine start-code scanner (`0x000001`) that requires no memory allocations or buffer copies.

## 3. SCTE-35 & Metadata Audit

Decodes Digital Program Insertion markers and calculates the absolute PTS alignment error between the splice command and the actual video IDR frame.
