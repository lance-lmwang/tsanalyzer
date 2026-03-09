# ES & T-STD Model Refactor Spec (Ultimate Director's Edition)

## 1. Core Objectives
- **Metrology Integrity (Phase 0)**: Resolve LRM (Linear Regression Model) numerical instability using baseline offsets.
- **T-STD Compliance**: Implement TB (Transport), MB (Multiplexing), and EB (Elementary) buffer models per ISO/IEC 13818-1 Annex D.
- **AU-Aware Analytics**: Identify Access Unit (AU) boundaries and GOP structures (N/M) for H.264/H.265.

## 2. Technical Models

### 2.1 Refined LRM (Phase 0)
- **Baseline Alignment**: $T_{base} = samples[ oldest\_idx ]$. All calculations use $(X - X_{base})$ and $(Y - Y_{base})$ to preserve nanosecond precision in `double` operations.
- **No Feedback Loop**: The regression window MUST only contain **real observed PCR samples**. Predictive/Unwrapped values are strictly forbidden for regression.

### 2.2 T-STD Leaky Bucket & Removal
- **Leak Rate ($R_{rx}$)**: Synchronized with the high-precision Physical Bitrate from the Metrology Engine.
- **AU Removal**: At the exact moment of $t = DTS$, remove precisely one AU ($Size(AU)$) from the EB.
- **DTS Extrapolation**: If DTS is missing in the PES header, it must be extrapolated using the sequence frame rate: $DTS_{n} = DTS_{n-1} + Duration_{frame}$.

### 2.3 Buffer Violation Logic
- **Overflow**: Triggered when $F(t) > Buffer\_Size_{standard}$ for the specific level (TB/MB/EB).
- **Underflow**: Triggered when $DTS_{n} < STC(t)$, meaning the data required by the decoder has not arrived by its scheduled removal time.

## 3. Data Structures

### 3.1 Consolidated `tsa_es_track_t`
```c
typedef struct {
    uint16_t pid;
    uint8_t codec_type;

    /* Layer 1: PES/AU Accumulator (Zero-Copy) */
    struct {
        uint8_t state;           // HUNTING, ACCUMULATING, FINISHING
        uint64_t last_pts;
        uint64_t last_dts;
        uint32_t bytes_received;
        bool has_pts;
        bool has_dts;
    } accumulator;

    /* Layer 2: T-STD Simulation (Fixed-point Q64) */
    struct {
        int128_t tb_fill;        // Transport Buffer fill level
        int128_t mb_fill;        // Multiplexing Buffer fill level
        int128_t eb_fill;        // Elementary Buffer fill level
        uint64_t last_leak_vstc;
        bool sync_lost;          // Mark as sync-lost on CC errors
        bool violation_active;   // Overflow/Underflow status
    } tstd;

    /* Layer 3: GOP Analysis (Temporal analytics) */
    struct {
        uint32_t gop_n;          // Number of frames in GOP
        uint32_t gop_m;          // Distance between anchors
        uint64_t last_idr_ns;    // Timestamp of last IDR
        uint32_t idr_interval_ms;
        uint8_t last_slice_type;
    } gop;
} tsa_es_track_t;
```

### 3.2 Unified Frame Abstraction `tsa_au_t`
```c
typedef struct {
    uint64_t pts_ns;
    uint64_t dts_ns;
    uint32_t size;
    uint8_t frame_type;      // I, P, B, IDR
    bool is_idr;
    uint64_t pos_in_stream;  // Byte offset for forensic correlation
} tsa_au_t;
```

## 4. Error Handling
- **CC Error**: Immediately mark `tstd.sync_lost = true`. Reset and re-sync the T-STD model only when the next PUSI is detected and a valid PES header is successfully parsed.
- **Discontinuity**: Upon detecting a discontinuity indicator, re-align the physical leak rate $R_{rx}$ with the latest metrology anchor.

## 5. Validation Criteria
- **Bitrate Coupling**: T-STD leak must strictly follow the calculated physical bitrate.
- **Deterministic Removal**: Verify that buffer fill levels drop precisely at the DTS timestamp.
- **Zero-Copy Performance**: No extra memory allocation or `memcpy` during PES payload reassembly.
