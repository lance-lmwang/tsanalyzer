# PCR Metrology & Clock Reconstruction Spec (Refactor)

## 1. 核心目标 (Core Objectives)
- **消除歧义 (Disambiguation)**: 统一 27MHz Ticks、纳秒 (ns) 和 42-bit 原始值的转换逻辑。
- **状态隔离 (Isolation)**: 在 MPTS 模式下，每个节目 (Program) 拥有独立的时钟恢复基准 (STC)，严禁全局变量乱跳。
- **算法升级 (Algorithm Upgrade)**: 将目前离散的 Alpha-Beta 滤波器统一为基于滑动窗口的线性回归模型 (LRM)。

## 2. 核心数学模型 (Mathematical Model)

### 2.1 时钟重建 (STC Recovery)
使用线性回归 $C(t) = s \times T_a + b$：
- $T_a$: 本地到达时间 (System Arrival Time, ns)。
- $C(t)$: 重建后的系统参考时间 (Reconstructed STC, ns)。
- $s$: 时钟漂移斜率 (Slope)，理论值为 1.0。漂移值 $Drift_{ppm} = (s - 1.0) \times 10^6$。
- $b$: 拦截点 (Intercept)，代表相位偏移。

### 2.2 抖动计算 (Jitter Decomposition)
- **绝对误差 (Accuracy)**: $Err_{acc} = PCR_{observed} - C(t_{arrival})$。
- **残差抖动 (Residual Jitter)**: 消除漂移趋势后的差值。

## 3. 标准单位与数据结构 (Standard Units & Data Structures)

### 3.1 统一单位
- **Time**: 强制使用 `uint64_t` nanoseconds。
- **Frequency**: 27,000,000 Hz (PCR Ticks)。
- **Bitrate**: `uint64_t` bps (bits per second)。

### 3.2 建议的聚合结构 `tsa_pcr_track_t`
```c
typedef struct {
    uint32_t pid;
    uint32_t program_id;      // 所属节目 ID，用于 MPTS 聚合
    
    /* Layer 1: Raw Sampling */
    uint64_t last_pcr_ticks;  // 原始 42-bit Ticks
    uint64_t last_arrival_ns; // 系统 CLOCK_MONOTONIC 纳秒
    
    /* Layer 2: Clock Domain (Locked) */
    struct {
        double slope;         // 线性回归斜率
        double intercept;     // 相位偏移
        uint64_t stc_ns;      // 该 PID 对应的虚拟 STC
        bool locked;          // 锁定状态 (基于 LRM)
    } clock;

    /* Layer 3: Metrology Metrics */
    float jitter_ms;          // 抖动 (TR 101 290 P2.1)
    double drift_ppm;         // 漂移 (TR 101 290 P2.1b)
    uint64_t bitrate_bps;     // 平滑后的业务码率
} tsa_pcr_track_t;
```

## 4. 验证标准 (Validation Standards)
- **P1**: 必须通过 `scripts/verify_pcr_repetition.sh` (P1.1 错误)。
- **P2**: 必须通过 `scripts/verify_es_accuracy.sh` (PTS 对齐误差 < 1ms)。
- **Metrology**: 物理码率统计结果在 500ms 窗口内的方差应 < 1%。
