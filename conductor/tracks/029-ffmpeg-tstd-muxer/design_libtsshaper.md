# Engineering Design: LibTSMux (Broadcast-Grade TS Shaper)

## 1. Executive Summary
`libtsmux` is a high-performance software library designed to provide hardware-level precision for MPEG-TS traffic shaping and multiplexing. It transforms loosely timed or variable bitrate (VBR) TS streams into strictly compliant, low-jitter Constant Bitrate (CBR) streams. It is designed for mission-critical broadcast environments where T-STD compliance and PCR precision are mandatory.

## 2. System Architecture
The library utilizes a pipeline-oriented architecture to process and shape packets with nanosecond precision.

```text
       [ FFmpeg / VBR TS Input ]
                   |
     ┌─────────────▼─────────────┐
     │ Ingest + Metadata Parser  │ (PAT/PMT/PCR Identification)
     └─────────────┬─────────────┘
                   |
     ┌─────────────▼─────────────┐
     │      T-STD Simulator      │ (Buffer Safety & Overflow Protection)
     └─────────────┬─────────────┘
                   |
     ┌─────────────▼─────────────┐
     │      StatMux Engine       │ (Dynamic Bandwidth Allocation)
     └─────────────┬─────────────┘
                   |
     ┌─────────────▼─────────────┐
     │      TS Interleaver       │ (Priority-based Scheduling)
     └─────────────┬─────────────┘
                   |
     ┌─────────────▼─────────────┐
     │      PCR Restamper        │ (Jitter Reduction < 50ns)
     └─────────────┬─────────────┘
                   |
     ┌─────────────▼─────────────┐
     │        CBR Pacer          │ (High-Precision Timing)
     └─────────────┬─────────────┘
                   |
     ┌─────────────▼─────────────┐
     │   Network Output Sender   │ (UDP / RTP / SRT)
     └───────────────────────────┘
```

## 3. Core Functional Modules

### 3.1 T-STD Simulation (Decoder Safety)
Ensures the output stream never violates the Transport System Target Decoder (T-STD) buffer limits (ISO/IEC 13818-1).
- **Model**: Tracks VBV/CPB buffer fullness for every elementary stream.
- **Formulas**:
    - `Consumption`: `fullness -= leak_rate * dt`
    - `Arrival`: `fullness += packet_size * 8`
- **Logic**: The scheduler uses buffer risk levels to prioritize packets. If a buffer is near overflow, transmission is throttled; if near underflow, it is prioritized.

### 3.2 PCR Precision & PLL (< 50ns)
Eliminates jitter introduced by upstream software processing.
- **PCR PLL**: Maps the system monotonic clock to a stable 27MHz program clock reference.
- **Rewriting**: PCRs are re-stamped at the exact moment of packet egress.
- **Smoothing**: Employs a low-pass filter to ensure the PCR drift remains within ±500ns and jitter within ±50ns.

### 3.3 StatMux (Statistical Multiplexing)
Manages bandwidth distribution across multiple programs within a shared transport stream.
- **Allocation**: `bitrate_i = total_bitrate * (complexity_i / Σcomplexity)`.
- **Dynamic Range**: Supports minimum and maximum bitrate constraints per program.

### 3.4 TS Interleaver (Priority Scheduling)
Unlike a standard FIFO, the interleaver selects packets based on an "Urgency Score":
1. **Critical**: PCR PID (Precise timing required).
2. **High**: Audio (Small buffers, sensitive to starvation).
3. **Medium**: Video (Main payload).
4. **Low**: PSI/SI (PAT, PMT, SDT repetition).
5. **Padding**: NULL packets (PID 0x1FFF) to maintain CBR.

## 4. Network Output & Encapsulation

### 4.1 UDP / RTP
- **RTP**: Encapsulates 7 TS packets per frame (1316 bytes payload).
- **Timestamping**: 90kHz RTP timestamps derived from the smoothed PCR.
- **Efficiency**: Uses Linux `sendmmsg()` for batch processing to minimize syscall overhead.

### 4.2 SRT (Secure Reliable Transport)
- **Integration**: Native support for SRT (Caller/Listener/Rendezvous modes).
- **Pacing**: While SRT provides its own flow control, `libtsmux` ensures the *input* to the SRT buffer is a perfectly shaped TS stream, reducing the work required by the SRT retransmission logic.
- **Efficiency**: Utilizes `srt_sendmsg2` with `SRT_MSGCTRL` for optimized network delivery.

## 5. C API Specification (ABI Stable)

```c
typedef struct tsa_shaper tsa_shaper_t;

// Context Management
tsa_shaper_t* tsa_shaper_create(uint64_t total_bitrate);
void tsa_shaper_destroy(tsa_shaper_t* ctx);

// Program & Bitrate Control
int tsa_shaper_add_program(tsa_shaper_t* ctx, int program_id);
int tsa_shaper_set_program_bitrate(tsa_shaper_t* ctx, int program_id, uint64_t bps);

// Data Ingest (Opaque Push)
int tsa_shaper_push(tsa_shaper_t* ctx, int program_id, const uint8_t* ts_packet);

// Output Protocol Selection
typedef enum {
    TSA_OUT_UDP,
    TSA_OUT_RTP,
    TSA_OUT_SRT,
    TSA_OUT_FILE
} tsa_output_mode_t;

int tsa_shaper_set_output(tsa_shaper_t* ctx, tsa_output_mode_t mode, const char* url);

// Performance Monitoring
typedef struct {
    uint64_t bytes_sent;
    double pcr_jitter_ns;
    double buffer_fullness_avg;
} tsa_shaper_stats_t;

void tsa_shaper_get_stats(tsa_shaper_t* ctx, tsa_shaper_stats_t* stats);
```

## 6. Performance Targets
| Metric | Target |
| :--- | :--- |
| **Throughput** | > 5 Gbps |
| **PCR Jitter** | < 50 ns |
| **Bitrate Accuracy** | < 0.1% |
| **End-to-End Latency** | < 50 ms |
| **StatMux Programs** | Up to 128 programs per TS |
