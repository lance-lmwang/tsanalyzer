# Technology Stack

## Core Development
- **Language**: C11 (Primary), C++ (Secondary for glue/compatibility).
- **Standards**: ISO/IEC 13818-1 (MPEG-TS), TR 101 290 (Metrology).
- **Build System**: CMake (minimum 3.10).

## Networking & Transport
- **SRT (Secure Reliable Transport)**: Primary protocol for high-performance TS ingest and egress.
- **Mongoose**: Lightweight HTTP/Websocket server for API endpoints.
- **Pthreads**: Multithreaded engine execution model.

## Platform & Optimization
- **Linux**: Target operating system with specific tuning for NUMA and high-performance packet processing.
- **Deterministic Engine**: Custom C logic ensuring bit-identical results across PCAP replays.
- **Zero-Alloc Architecture**: In-place initialization and flat dispatch patterns to ensure mission-critical stability and performance.

## Testing & Verification
- **CTest**: Integrated testing framework within CMake.
- **Standalone Verification Tools**: Dedicated tools for metrological accuracy and reproducibility testing.
