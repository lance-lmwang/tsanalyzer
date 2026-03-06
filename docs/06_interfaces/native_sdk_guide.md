# Native SDK Integration Guide

The TsAnalyzer Engine is designed to be embedded into professional broadcast controllers, cloud-native transcoders, and legal recording systems.

---

## 1. C-ABI Linkage

The core engine is exposed as a thread-safe, high-performance C library (`libtsa_engine.so`).

### 1.1 Core Handle Initialization
```c
tsa_config_t cfg = { .op_mode = TSA_MODE_LIVE };
tsa_handle_t* h = tsa_create(&cfg);
```

### 1.2 Data Feeding
The integrator is responsible for feeding raw TS buffers into the engine.
```c
// Zero-copy pointer passing
void tsa_feed_data(tsa_handle_t* h, const uint8_t* buf, int len, uint64_t hat_ns);
```

---

## 2. Asynchronous Event Model

The SDK utilizes an event-callback registration system to notify the host application of metrology violations.

### 2.1 Event Registration
```c
void on_alarm(void* user_data, int event_type, uint16_t pid);
tsa_set_callback(h, on_alarm, context);
```

### 2.2 Snapshot Retrieval
The host can poll the engine for an atomic metrology state.
```c
tsa_snapshot_t snap;
tsa_take_snapshot(h, &snap);
```

---

## 3. Language Bindings

Official wrappers are provided for seamless integration with high-level ecosystems:
*   **Python**: Via `ctypes` wrapper for rapid prototyping.
*   **Java (JNI)**: For deployment in enterprise broadcast management systems.
*   **Go (CGO)**: For cloud-native observability platforms.
