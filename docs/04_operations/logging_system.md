# High-Performance Asynchronous Logging System (v2.4)

## 1. Design Goals
*   **Zero Blocking**: Real-time threads never wait. O(1) ingestion, no locks, no atomic CAS on the producer path.
*   **High Signal & Low Contention**: Distributed thread-local buffers with cacheline alignment.
*   **Self-Healing**: Proactive disk monitor with hysteresis and signal-safe crash recovery.
*   **Industrial Throughput**: > 1,000,000 logs/sec with < 100ns producer latency.

---

## 2. Architecture: Distributed SPSC Ingestion

### 2.1 Thread-Local Buffers (Lock-Free)
Each thread maintains its own SPSC (Single-Producer Single-Consumer) ring buffer to eliminate cross-thread contention.
*   **Alignment**: `head` and `tail` pointers are aligned to 64-byte cachelines to prevent **False Sharing**.
*   **Life-cycle Safety**: The Global Registry uses a `slots` architecture with an `active` flag to prevent `use-after-free` when threads exit.

### 2.2 Global Registry
```c
typedef struct {
    tsa_ringbuffer_t *rb;
    _Atomic bool active;
    uint32_t thread_id;
} tsa_log_slot_t;

typedef struct {
    tsa_log_slot_t slots[TSA_MAX_THREADS];
    _Atomic uint32_t count;
} tsa_log_registry_t;
```

---

## 3. Data & Protection

### 3.1 Structured Log Entry
```c
typedef struct {
    uint64_t ts_ns;       // Nanoseconds
    uint64_t context_id;  // Hash of stream/request
    uint32_t pid;         // MPEG-TS PID
    uint32_t tid;         // OS Thread ID
    uint16_t level;       // TRACE to ERROR
    char tag[16];
    const char *file;
    uint32_t line;
    char msg[256];        // [TRUNCATED] added if exceeds 256
} tsa_log_entry_t;
```

### 3.2 Error Storm & Sampling
*   **Rate Limiting**: Limit identical logs (same `hash(tag + level + msg[0..31])`) to 100/sec.
*   **Sampling**: If worker throughput > 100k/sec, switch to 10% sampling mode.

### 3.3 Disk Protection (with Hysteresis)
*   **Critical (< 2%)**: Disable file sink. **Recover only when > 8%**.
*   **Warning (< 5%)**: Switch to `ERROR-ONLY` mode.

---

## 4. Operational Telemetry & Sinks

### 4.1 JSON Sink Example
```json
{
  "ts": 1712345678000,
  "level": "WARN",
  "tag": "PCR",
  "context": "0x91ab23",
  "pid": 256,
  "thread": 7,
  "msg": "clock drift detected [1.2ms]"
}
```

### 4.2 Metrics (Prometheus)
*   `tsa_logging_queue_usage`: buffer occupancy.
*   `tsa_logging_dropped_total`: overflow drops.
*   `tsa_logging_rate_limited_total`: suppressed logs count.
*   `tsa_logging_sink_rate`: logs/sec throughput.

---

## 5. Implementation Phases
1.  **Phase 1**: SPSC RingBuffers with `alignas(64)` and Safe Registry.
2.  **Phase 2**: Log Worker (Batch Drain) & Stdout Sink.
3.  **Phase 3**: File Sink, Rotation (64MBx20), and Fsync Policy.
4.  **Phase 4**: Rate Limiting (Hash-based) and Disk Hysteresis.
5.  **Phase 5**: Signal-Safe Crash Handler and Metrics Export.
6.  **Phase 6**: Per-Tag Log Levels and JSON Sink.
