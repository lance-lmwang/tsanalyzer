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
## 3. Advanced Metadata & Ancillary Audit

Beyond standard essence tracking, the engine performs deep-mining of the bitstream to ensure professional broadcast compliance.

### 3.1 CEA-608/708 Caption Fidelity
The `NALU Sniffer` identifies SEI messages containing Closed Caption (CC) data.
*   **Audit**: Verifies the presence and continuity of CEA-708 service blocks synchronized with Video IDR frames.
*   **Alarm**: Triggers `CAPTION_MISSING` or `CAPTION_STUTTER` if the payload cadence deviates from the frame rate.

### 3.2 SEI Latency & Timing Marks
Extracts raw timestamps embedded in H.264/HEVC SEI messages (e.g., Picture Timing SEI).
*   **Measurement**: Correlates the encoder's internal timestamp with the reconstructed 27MHz STC.
*   **Metric**: Detects **Encoding Latency Fluctuations** and source-side buffer instability.

### 3.3 SMPTE 2038 Ancillary Data
Identifies and audits PIDs containing SMPTE 2038 data structures.
*   **VANC Detection**: Extracts Vertical Ancillary data such as AFD (Active Format Description) and SCTE-104 ad-triggers.
*   **Compliance**: Ensures the ancillary data PID bitrate remains within the required operational window.

---

## 5. Deep Metadata Sidecars (MediaInfo/FFprobe)

To provide "Deep Static Analysis" alongside real-time metrology, the engine integrates external metadata toolsets.

### 5.1 Triggered Inspection
*   **Event**: When a new PID is discovered or a significant codec change occurs.
*   **Action**: The Analysis Plane spawns an asynchronous Task to run `libmediainfo` or `ffprobe` on a 500ms cached segment of the stream.
*   **Result**: A comprehensive property report (Bit depth, HDR profile, Frame structure) is merged into the stream's JSON metadata.

### 3.4 DVB Service Inventory
The engine reconstructs the logical service hierarchy from the SDT (Service Description Table).
*   **Metadata Extraction**: Captures `Service Name` and `ServiceProvider` for every program.
*   **Character Set Decoding**: Implements **ISO-8859** and DVB-specific character table mapping to ensure correct UTF-8 representation of regional channel names.

### 3.5 CAS & Scrambling Visibility
To audit encrypted services, the engine monitors Conditional Access (CA) descriptors and transport-level flags.
*   **Scrambling State**: Real-time monitoring of the `transport_scrambling_control` bits in every TS packet header.
*   **CAID Audit**: Identifying the CA system IDs (e.g., Irdeto, Nagra, Viaccess) associated with every scrambled PID via the PMT/CAT.
*   **Alarm**: Triggers `SERVICE_UNEXPECTEDLY_SCRAMBLED` if a service marked as Free-to-Air (FTA) starts using encryption.

### 3.6 EPG & Content Signaling (EIT)
The engine monitors the Event Information Table (EPG) to ensure that the broadcast schedule metadata is correctly populated.
*   **Audit**: Verifies the presence of `present/following` events and the repetition rate of the EIT schedule.
*   **Signaling Accuracy**: Cross-checks the EIT `start_time` and `duration` against the actual video stream discontinuities.

### 3.7 T2-MI Interface Analysis
For DVB-T2 distribution links, the engine decodes the T2-MI (T2 Modulator Interface) encapsulation.
*   **Analysis**: Audits the T2-MI baseband frames, L1-signaling, and the consistency of the PLP (Physical Layer Pipe) distribution.
*   **Detection**: Identifies packet loss within the T2-MI tunnel that might be hidden from standard TS analyzers.

---

## 4. SCTE-35 & Metadata Audit
Decodes Digital Program Insertion markers and calculates the absolute PTS alignment error between the splice command and the actual video IDR frame.
