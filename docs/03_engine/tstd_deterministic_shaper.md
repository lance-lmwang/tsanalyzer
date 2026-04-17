# T-STD 确定性物理整形器规范（v6.0 - Industrial Standard）

---

## 1. 核心目标：物理层零抖动与微观平滑
*   **PCR Jitter**: < 100ns (目标 0ns via Fractional STC).
*   **Bitrate Stability**: 1s 窗口波动 ≤ ±4%.
*   **Micro-Smoothness**: 100ms 瞬时低谷（Min_Dip）必须 > Target * 70%.

---

## 2. 总体架构 (The Pipeline)
`Fractional STC (Bresenham)` → `ΣΔ Pacing Engine` → `Closed-Loop Rate Feedback` → `Physical Emission`.

---

## 3. 物理层审计标准 (Metrology L1 Standard)

### 3.1 核心测量模型：梯形积分 (Trapezoidal Integration)
审计仪不再采用简单的包计数，而是模拟 T-STD 排空过程：
$$Rate(t) = \frac{\int_{t-W}^{t} Bits(\tau) d\tau}{W}$$
其中 $W$ 为滑动窗口宽度（1s 或 100ms）。

### 3.2 判定状态 (Diagnosis States)
| 状态标记 | 触发条件 | 物理含义 |
| :--- | :--- | :--- |
| **OK** | 均值正常 & 无微观坍塌 | 广播级输出，完美 CBR。 |
| **! LOW !** | $Vid\_1s < Target \times 0.9$ | **物理上限压制**：通常由 `min_gap` 设置过大或 `window_bits` 策略过严导致。 |
| **!!! DIP !!!** | $Min\_100ms < Target \times 0.6$ | **瞬时物理空洞**：由于带宽被 SI/PCR 挤占且视频无“补课”能力，导致解码器端可能卡顿。 |
| **!!! STALL !!!** | $Rate < 10\text{kbps}$ | **逻辑锁死或断流**：Muxer 停止输出视频负载，仅保留 PCR/NULL。 |
| **TIME_JUMP** | $\Delta PCR > 10\text{s}$ | **时钟断裂**：PCR 时间轴非线性跳变，属于底层引擎严重错误。 |

---

## 4. 控制层逻辑实现

### 4.1 ΣΔ 积分器 (Sigma-Delta)
$$err\_acc \leftarrow err\_acc + \frac{Shaping\_Rate}{Muxrate}$$
*   **准入**: $err\_acc \ge 1.0$。
*   **对冲**: 发包后 $err\_acc \leftarrow err\_acc - 1.0$。

### 4.2 闭环速率守卫 (Rate Feedback Guard) - NEW
为防止 1.2x OPR 导致的“全油门”超标，在 `pick_es_pid` 增加闭环限制：
```c
if (pid->window_bits > pid->bitrate_bps)
    continue; // 达到 1s 额度上限，强制点刹
```

### 4.3 物理间距守护 (Gap Guard)
*   **v4.0**: $min\_gap = 2$（导致 600k 瓶颈）。
*   **v6.0**: $min\_gap = 1$（释放带宽，允许自愈补课）。

---

## 5. 验证指标 (KPIs)
1.  **MEANk**: 必须落入 $[Target \times 0.99, Target \times 1.01]$。
2.  **SCORE**: $(Max - Min) + 2\sigma < 50.0$。
3.  **Jitter**: 必须严格为 $0$（基于物理位置同步）。
