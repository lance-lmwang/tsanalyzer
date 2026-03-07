# Track Specification: Industrial Integration (ltntstools)

## 1. Goal
Integrate best-in-class architectural patterns from `libltntstools` into `tsanalyzer` to achieve broadcast-grade reliability, precision, and observability.

## 2. Key Design Patterns to Adopt

### A. High-Precision Throughput & Bitrate Histograms
-   **Concept**: Use sliding window histograms instead of simple 1s averages.
-   **Value**: Detect micro-bursts and peak bitrates that cause network drops.
-   **Implementation**: Add `Peak Bitrate` and `Bitrate Distribution Buckets` to Prometheus metrics.

### B. Extensible Descriptor Factory
-   **Concept**: Abstract PMT/SDT descriptor parsing into a handler-based registry.
-   **Value**: Professional support for LCN, Parental Rating, and other DVB extensions without bloating the core parser.
-   **Implementation**: Create `tsa_descriptor_handler_t` interface.

### C. Zero-Alloc Object Reuse with Side-band Metadata
-   **Concept**: Pre-allocated object pools using `xorg-list` (or similar) for zero `malloc/free` during hot-path processing.
-   **Value**: Eliminate memory fragmentation and improve cache locality.
-   **Implementation**: Link analysis results (side-band) directly to raw TS packets in the pool.

## 3. Reference Implementation
-   `libltntstools/src/throughput_hires.c`
-   `libltntstools/src/descriptor.c`
-   `libltntstools/src/kl-queue.c`
-   `libltntstools/src/smoother-pcr.c` (Clocks/Drift)
