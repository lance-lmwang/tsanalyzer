# Virtual Time Domain & Verification Architecture

## 1. Executive Summary
In professional broadcast infrastructure, compliance cannot rely solely on mathematical models; it must be empirically verified on the physical wire. This document details the **Virtual Time Domain** testing architecture for `libtsshaper`. It allows 100% automated, deterministic TR 101 290 validation within standard CI/CD pipelines (e.g., GitHub Actions) without relying on expensive physical stream analyzers (like Tektronix or DekTec).

The architecture also covers the commercial deployment strategy, detailing the integration of `libtsshaper` into the FFmpeg ecosystem via `AVIOContext` interception.

## 2. PCAPNG Mock HAL (Virtual Pacer)
In a CI/CD environment, wall-clock timing (`clock_gettime` or CPU sleep loops) is unreliable due to container resource constraints. To solve this, we implement a **Mock HAL** that bypasses `sendmmsg()` and writes directly to a high-precision PCAPNG file.

### 2.1 Why PCAPNG?
Traditional PCAP files (Magic Number `0xa1b2c3d4`) only support microsecond resolution. PCAPNG allows setting the Interface Description Block (IDB) `if_tsresol` to `9` (nanoseconds, $10^{-9}$), precisely preserving the exact theoretical emission time ($T_{emit}$) calculated by the Layer 3 Pacer.

### 2.2 C11 Implementation Strategy
During testing, the engine injects a mock function that constructs the PCAPNG binary format on the fly. It wraps the 1316-byte UDP payloads (7 TS packets) with dummy Ethernet, IPv4, and UDP headers, stamping them with the absolute $T_{emit}$ nanosecond timestamp.

## 3. Nanosecond PCR Jitter Analyzer
To validate the generated PCAPNG artifact, a dedicated Python analyzer acts as an objective "third-party referee." It verifies not only TS syntax but the absolute physical jitter in the nanosecond time domain.

### 3.1 Mathematical Model
- **The Anchor**: $T_{pcap\_0}$ (Physical capture time) and $T_{pcr\_0}$ (Decoded PCR time) of the first packet.
- **Expected Time**: For the $i$-th packet: $T_{expected\_i} = T_{pcap\_0} + (T_{pcr\_i} - T_{pcr\_0})$.
- **Absolute Jitter**: $Jitter_i = T_{pcap\_i} - T_{expected\_i}$.
- **Compliance Threshold**: DVB requires $|Jitter_i| \le 500ns$. `libtsshaper` enforces $|Jitter_i| \le 30ns$.

### 3.2 Python Analyzer (Scapy)
The automated script utilizes the `scapy` library to read PCAPNG files with high-precision `Decimal` timestamps. It manually parses the 188-byte TS payload to extract the 42-bit PCR value and converts it from 27MHz ticks to nanoseconds.

**CI/CD Assertions:**
1. **PCR Interval**: Fails if the physical interval between two consecutive PCRs exceeds 40ms.
2. **PCR Jitter**: Fails if the maximum observed jitter exceeds 30ns.
3. **TR 101 290**: The artifact is concurrently fed into `TSDuck` (`tsp -I pcap ... -P analyze --tr101290`) to strictly assert zero Priority 1/2/3 semantic errors.

## 4. FFmpeg Ecosystem Integration
To transform `libtsshaper` into a fully-fledged commercial encoder, it is seamlessly integrated with the FFmpeg ecosystem, intercepting the stream at the I/O boundary.

### 4.1 Architecture Flow
```text
[ FFmpeg A/V Encoders (libavcodec) ]
               |
               v (AVPacket)
[ FFmpeg TS Muxer (libavformat) ]     <-- Disables CBR padding and PCR alignment
               |
               v (188-byte VBR TS Packets via AVIOContext)
====================================================================
               |  (Boundary: Custom AVIO write_packet callback)
               v
[ libtsshaper Ingest (Layer 1) ]      <-- Receives VBR packets, applies semantics (Hint Mode)
               |
               v
[ libtsshaper Engine (Layer 2 & 3) ]  <-- T-STD modeling, prioritization, PCR rewriting
               |
               v
[ Physical Network (sendmmsg) ]       <-- Nanosecond-precise CBR emission
```

### 4.2 Integration Gotchas
- **Disable FFmpeg Muxer Shaping**: FFmpeg's internal CBR muxing (`mpegtsenc.c`) must be aggressively disabled. It acts solely as a "Syntax Packer" outputting VBR streams.
- **AVIOContext Interception**: Use `avio_alloc_context()` to route all output through a custom `write_packet` callback directly into `tsshaper_push()`.
- **Semantic Hinting**: The custom AVIO callback peeks into the TS header PID to classify the packet (e.g., `TSS_PID_TYPE_PSI_SI` vs. `TSS_PID_TYPE_VIDEO`) and feeds this hint into the shaper engine, bypassing complex payload deep-inspection.
