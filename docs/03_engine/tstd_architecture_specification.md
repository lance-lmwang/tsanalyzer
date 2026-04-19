# T-STD Multiplexer Architecture Specification

## 1. Physical Timing Model
The T-STD engine implements a **Unified Physical Timeline**. It decouples the encoder's variable-rate stream from the multiplexer's constant-rate physical emission using a virtual System Time Clock (vSTC).

### 1.1 The vSTC Core
The `vSTC` represents the theoretical emission clock of the multiplexer. It increments based on the physical byte count emitted:
$$vSTC_{now} = vSTC_{init} + \frac{Bytes_{emitted} \times 8}{Muxrate}$$

### 1.2 Delay-Adaptive Pacing
To maintain VBV compliance while maximizing ES layer smoothness, the engine utilizes a **Delay-Adaptive PI Controller**. Unlike static regulators, this pacer scales its reactivity based on the user-configured `muxdelay`.
*   **Target Occupancy**: 70% of total buffer depth.
*   **Adaptive Corridor**: The emission rate is clamped to a range that inversely scales with buffer depth, ensuring the engine never attempts a correction that the physical buffer cannot absorb.

## 2. Shared PID Synchronization
In professional broadcast environments, PCR and Video frequently share the same PID. The engine implements **Transparent Continuity Counter (CC) Sniffing** to prevent stream corruption:
*   **Observation**: Real-time tracking of the CC from incoming payload packets.
*   **Injection**: PCR-only packets utilize the last known CC without incrementing, ensuring zero packet-corrupt alarms in downstream analyzers.

## 3. Compliance and Accuracy
The engine is calibrated to meet industrial-grade bitrate stability and TR 101 290 Priority 1/2 requirements.
*   **Bitrate Precision**: Stable within ±4% of target under long-term stress.
*   **PCR Accuracy**: Jitter suppressed via physical slot-level injection.
*   **Recovery**: Integrated **Voter Mechanism** for multi-PID discontinuity consensus.
