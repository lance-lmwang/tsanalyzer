# Engineering Design: LibTSShaper (T-STD Compliant Architecture)

## 1. Introduction
This document defines the architecture for `libtsshaper`, a broadcast-grade MPEG-TS traffic shaper. The primary goal is to generate Transport Streams that strictly adhere to the **ISO/IEC 13818-1 T-STD (Transport System Target Decoder)** model, ensuring zero buffer violations (TB/MB/EB) and precise jitter control.

## 2. Theoretical Model (The "Why")

### 2.1 The T-STD Challenge
A Transport Stream flows at a constant channel rate ($R_{ts}$), e.g., 50 Mbps. However, individual elementary streams (Video, Audio) inside it have much lower average rates ($Rx_n$), e.g., 5 Mbps.

The T-STD model defines a **Transport Buffer ($TB_n$)** for each PID $n$:
- **Size**: Fixed at **512 bytes** for ISO/IEC 13818-1 compliance.
- **Input**: Data enters at $R_{ts}$ (50 Mbps).
- **Leak**: Data leaves at $Rx_n$ (5 Mbps).

**The Buffer Equation:**
$$ B(t) = \int_{0}^{t} (R_{in}(\tau) - R_{out}(\tau)) d\tau $$

If the muxer sends 3 video packets back-to-back at 50 Mbps:
- Time to input: $3 \times 188 \times 8 / 50M \approx 90 \mu s$
- Time to leak: $3 \times 188 \times 8 / 5M \approx 900 \mu s$
- The buffer accumulates faster than it drains. Since 564 bytes > 512 bytes, **TB Overflows**.

**Conclusion:**
The muxer **cannot** send packets for PID $n$ faster than $Rx_n$ allows. It must insert "gaps" (NULL packets or packets from other PIDs) between video packets. The output bitrate of the video PID, when measured over short windows, must look like a "straight line" at $Rx_n$, not a square wave.

## 3. Architecture Redesign

### 3.1 Shaping Core: The "Multi-Leaky-Bucket" Scheduler
Instead of a simple Weighted Fair Queuing (WFQ) or Priority queue, we implement a strict **Time-Division Multiplexing (TDM)** scheduler driven by Leaky Buckets.

#### Data Structures
For each PID $n$:
- `queue`: SPSC buffer of pending TS packets.
- `leak_rate` ($Rx_n$): The target shaping rate (e.g., 5 Mbps).
- `credit`: The number of bits allowed to be sent *now*.
- `tb_virtual_level`: Simulated level of the receiver's TB.

#### 3.2 The Scheduling Algorithm (Interleaver)
At every transport packet slot (defined by $1 / R_{ts}$):

1.  **Update Credits**:
    For all active PIDs, increase `credit` by:
    $$ \Delta_{credit} = Rx_n \times \Delta_{time} $$
    *(Clamp credit to prevent huge bursts after silence)*

2.  **Candidate Selection**:
    Identify all PIDs where:
    - `queue` is not empty.
    - `credit` >= 188 bytes.
    - `tb_virtual_level` + 188 < 512 (Strict TB Protection).

3.  **Arbitration**:
    If multiple PIDs are ready:
    - **Priority 0 (PCR)**: Always pick PCR-bearing packets first if compliant.
    - **Priority 1 (Audio/SI)**: Pick to minimize latency.
    - **Priority 2 (Video)**: Pick if available.

4.  **Action**:
    - **If a PID is picked**:
        - Pop packet.
        - Deduct 188 bytes from `credit`.
        - Update `tb_virtual_level`.
        - Output Packet.
    - **If NO PID is picked** (all are constrained by rate or empty):
        - **Output NULL Packet (0x1FFF)**. This is crucial. It means we have channel capacity ($R_{ts}$) but the elementary streams are "cooling down" to respect their T-STD limits.

### 3.3 Rate Configuration
To achieve a "Straight Line" video output:
- The user MUST specify the `leak_rate` ($Rx_n$) for the video PID.
- Example: Channel = 50 Mbps, Video = 5 Mbps.
- The shaper will emit: 1 Video Packet -> 9 NULL Packets -> 1 Video Packet...
- This results in a perfectly smooth 5 Mbps flow within the 50 Mbps pipe.

## 4. API Changes
We need explicit API controls for these parameters.

```c
// Configure the channel total rate (CBR)
tsshaper_create(total_bitrate = 50Mbps);

// Configure the video stream characteristics
// "I have a video stream on PID 100 that should be shaped to 5Mbps"
tsshaper_set_pid_rate(ctx, 0x100, 5000000);
```

## 5. Verification Metrics
Success is defined as:
1.  **Total Output**: Strict CBR at $R_{ts}$.
2.  **Video PID Output**: Strict CBR at $Rx_n$ (measured with 40ms window).
3.  **TB Analysis**: A simulation of the T-STD TB should never overflow 512 bytes.
