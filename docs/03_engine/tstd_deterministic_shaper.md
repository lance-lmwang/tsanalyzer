# T-STD 确定性物理整形器核心规范 (v3.1)

## 1. 核心架构：时钟驱动的物理整形 (Clock-Driven Shaping)
本设计将 T-STD 模型从传统的“按需调度器”升级为“时钟驱动物理整形”。系统模拟一个 **物理节拍发送系统 (Paced Transmission System)**，将总线带宽离散化为等间隔的原子槽位 (Slots)。

### 1.1 分数阶物理时钟 (Fractional Slot Timing)
为了消除 27MHz 时钟下因整数除法产生的相位漂移 (Phase Drift)，物理槽位跨度必须使用分数阶步进 $(N, R, D)$：
*   **Ticks Per Slot (N)**: `(1504 * 27000000) / Muxrate`
*   **Remainder (R)**: `(1504 * 27000000) % Muxrate`
*   **Denominator (D)**: `Muxrate`
*   **步进逻辑**:
    ```c
    stc_rem += R;
    if (stc_rem >= D) {
        stc_rem -= D;
        v_stc += (N + 1);
    } else {
        v_stc += N;
    }
    ```
    *该逻辑确保在任何长时间跨度下，物理时间轴偏差恒等于 0。*

## 2. 核心算法组件

### 2.1 锁相环 Pacer (Soft-PLL)
解决数据欠载导致的相位滞后与补偿性突发：
*   **锁相逻辑**: `next_send_stc = max(next_send_stc + ideal_interval, v_stc)`。
*   **目的**: 严禁在数据中断后产生瞬时高带宽“补课”脉冲，维持物理分布的稀疏性。

### 2.2 ΣΔ 流量控制器 (Sigma-Delta)
解决周期性量化震荡，实现极致稳定的 1s 滑动窗口指标：
*   **准入规则**: **禁止引入死区 (No Deadzone)**。
*   **控制决策**:
    - 记录 `Ideal_Target_Bits - Actual_Sent_Bits` 的累积误差 `err_acc`。
    - 只要 `err_acc` 积累到超过一个 TS 包的位宽，在当前槽位拥有准入权。
*   **目标**: 将 1s 采样窗口波动压制在 ±10k 级别，确保审计 Score < 50。

## 3. TDMA 物理层优先级 (Hardware-like Priority)
在每一个 Slot 周期，执行严格的确定性优先级判定，消除 PID 间的生存挤压：
1.  **Level 0: PCR/PSI** (物理位置强占，使用 `packet_index % interval == 0` 触发)。
2.  **Level 1: Audio** (高优异步插队，只要 Token 足够且 FIFO 非空，优先占用 Video 槽位)。
3.  **Level 2: Video** (ΣΔ 整形准入，受 Slot-Lock 和流量计双重约束)。
4.  **Level 3: NULL** (结构化物理填充，维持 CBR 恒定速率)。

## 4. Native 实现规范

### 4.1 TSTDPidState (PID 级状态)
| 变量名 | 类型 | 说明 |
| :--- | :--- | :--- |
| `next_send_stc` | `int64_t` | 锁相环准入时间戳 (27MHz) |
| `token_rem` | `double` | 令牌桶微比特补偿余数 |
| `err_acc_v3` | `double` | ΣΔ 累积误差器 (无死区) |

### 4.2 TSTDContext (物理引擎)
| 变量名 | 类型 | 说明 |
| :--- | :--- | :--- |
| `stc_rem` | `int64_t` | 分数阶时钟余数累积器 |
| `mux_rate` | `int64_t` | 唯一权威发包速率基准 |
| `pkt_index` | `int64_t` | 全局物理位置计数器 (TDMA 基准) |

## 5. 验证与审计标准 (Metrology Alignment)
*   **30ms 窗口**: 视频包分布偏差 $\pm 1$ 个包。
*   **1s 窗口**: 波动 (Fluctuation) $< \pm 48\text{kbps}$。
*   **质量评分**: $Score = (Max_{1s} - Min_{1s}) + StdDev_{1s} \times 2.0 < 50$。
*   **PCR 抖动**: 硬件级 0 抖动 ($< 100\text{ns}$)。
