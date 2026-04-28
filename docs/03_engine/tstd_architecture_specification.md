# T-STD Multiplexer Architecture Specification (Modular Refactoring)

## 1. Modular Engine Architecture
The V3 refactoring decomposes the monolithic T-STD logic into functional modules to ensure carrier-grade maintainability and testability.

### 1.1 Core Modules
- **`tstd_scheduler.c`**: Implements the Hierarchical Priority Decision Tree.
- **`tstd_pacing.c`**: Manages the Industrial Smoothing Chain and Token Bucket logic.
- **`tstd_voter.c`**: Handles multi-PID timestamp consensus and epoch translations.
- **`tstd_metrics.c`**: Provides high-resolution telemetry and metrology data.
- **`tstd_io.c`**: Manages low-level TS packet buffering and I/O.

## 2. Physical Timing Model (Flywheel)
The engine implements a **Unified Physical Timeline** via a virtual System Time Clock (vSTC), decoupled from input jitter.

### 2.1 Industrial Smoothing Chain (3-Stage)
Implemented in `tstd_pacing.c`, the refill rate undergoes:
1.  **Slew-Rate Limiter**: Caps instantaneous changes to 0.5% per step.
2.  **Low-Pass Filter (EMA)**: $\alpha = 1/16$ for mathematical smoothness.
3.  **Hard Clamp**: Strict $\pm 20\%$ safety bounds against configuration.

## 3. Discontinuity & Voter System
The `tstd_voter.c` module implements a consensus-based approach to source-level jumps:
- **Streak Consensus**: Prevents "instadeath" from single-packet jitter using a 3-packet verification streak.
- **Master/Slave Epoch Authority**: Ensures only the Master PCR PID can translate the global system clock, while others perform local absorption.

## 4. Congestion Management
- **L1 Emergency Preemption**: Allows ES data to temporarily "steal" PCR slots if queue delay exceeds 1000ms, preventing TB_n overflow.
- **AU-Locked Keyframe Tracking**: Synchronizes IDR status from the encoder layer to prevent accidental keyframe drops during congestion.
