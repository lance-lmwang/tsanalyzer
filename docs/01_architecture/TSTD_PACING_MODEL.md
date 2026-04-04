# T-STD Broadcast-Grade Pacing & Timing Model (v3.4)

This document defines the production-grade physical and logical constraints for the T-STD multiplexer, ensuring zero data loss and full timing compliance.

## 1. Synchronous CBR Engine (STC Progression)

In a broadcast-grade CBR multiplexer, the output bitstream IS the clock. The System Time Clock (STC) advances deterministically with every emitted packet.

### 1.1 Robust Anchoring (Master-First)
- **Anchor Source**: The primary stream carrying the PCR (Master Stream).
- **Initialization**: `stc_offset = first_master_dts_27m - mux_delay_27m`.
- **Phase Alignment**: Upon the first packet, `v_stc` and `last_v_stc` are both synchronized to `stc_offset` to start the physical progression from a clean zero-budget state.

### 1.2 STC Elasticity (No-Drop Policy)
- **Mechanism**: If a source packet arrives with a DTS dangerously close to current `v_stc` (gap < 20ms), the multiplexer performs an **STC Recalibration** by shifting `stc_offset` backwards. This ensures content integrity while maintaining the $PCR \le DTS$ causality.

## 2. PCR Precise Sampling

PCR $t(i)$ sampling occurs at the moment the 11th byte of the packet departs.
```
pcr_byte_tx_offset = (11 * 8 * 27,000,000) / mux_rate
pcr_ideal = v_stc + pcr_byte_tx_offset
```

## 3. Synchronous Scheduling (Layered Priority)

The scheduler operates on a **Fixed Slot** basis. Every invocation of the internal step function MUST emit exactly one 188-byte TS packet to advance the physical timeline.

### 3.1 Slot Selection Logic
1.  **L0 (PCR)**: Standalone AF-only packet if the PCR deadline is reached.
2.  **L1 (System)**: PSI/SI (PAT/PMT/SDT) if their respective repetition intervals are reached.
3.  **L2 (Elementary)**: A/V PES data if the PID's token bucket and $TB_n$ fullness allow.
4.  **L3 (Filler)**: Null packets emitted if no higher-layer data is ready for the current slot.

## 4. Constraint Enforcement (Per-PID)

- **Token Bucket**: Used strictly for PID-level rate shaping, NOT for gating the multiplexer bus.
- **TB_n Fullness**: PES packets are only eligible if the decoder's 512-byte transport buffer has sufficient space.
- **Clock Coupling**: `v_stc` is updated AFTER the packet is committed:
  `v_stc = stc_offset + (total_bytes_written * 8 * 27M) / mux_rate`.
