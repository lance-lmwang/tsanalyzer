# 终极设计文档：基于 ΣΔ 调制与分数阶时钟的确定性 TS 整形器 (v3.1)

## 1. 核心架构：确定性物理整形 (Deterministic Shaping)
本设计将 T-STD 从传统的“按需调度器”升级为“时钟驱动物理整形系统”。通过将物理带宽离散化为等间隔的分数阶原子槽位（Slots），彻底消除软件调度带来的随机抖动。

### 1.1 物理槽位常量定义 (Fractional Slot Timing)
为了消除整数除法带来的相位漂移（Phase Drift），物理槽位必须使用分数阶步进：
*   **Ticks Per Packet (N)**: `(1504 * 27000000) / Muxrate`
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

## 2. 核心模块设计

### 2.1 锁相环 Pacer (Soft-PLL)
解决数据欠载导致的相位滞后与补偿性突发：
*   **状态变量**: `next_send_stc` (PID 下一次准入时间)。
*   **更新规则**:
    - 发送成功: `next_send_stc += ideal_interval`
    - 数据欠载 (Underflow): `next_send_stc = max(next_send_stc, v_stc)`
*   **目的**: 确保视频包在物理轴上均匀分布，在数据中断恢复后，绝对不产生抢占式脉冲。

### 2.2 ΣΔ 流量控制器 (Sigma-Delta Controller)
为解决量化震荡问题，引入高精度 ΣΔ 调制逻辑：
*   **状态变量**: `quota_error_acc` (累计位宽误差)。
*   **原则**: **严禁引入死区 (No Deadzone)**。
*   **决策逻辑**:
    - 记录 `Ideal_Target_Bits - Actual_Sent_Bits` 的累积误差。
    - 当误差 > 1.0 个 TS 包时，强制在该 Slot 触发发送（若 FIFO 有数据）。
*   **目的**: 实现 1s 滑动窗口波动压制在 ±10k 级别，Score < 50。

### 2.3 TDMA 物理层优先级 (Hardware-like Priority)
在每一个 Slot 周期，执行严格的确定性优先级判定：
1.  **Level 0 (PCR/PSI)**: 物理序号强制预留 (`packet_index % interval == 0`)。
2.  **Level 1 (Audio)**: 高优异步插队，只要 Token 足够且 FIFO 非空，优先占用 Video 槽位。
3.  **Level 2 (Video)**: 受 Slot-Lock 和 ΣΔ 双重约束的平滑整形。
4.  **Level 3 (NULL)**: 结构化物理填充，维持 CBR 恒定速率。

## 3. 关键数据结构 (Native C 实现)

### 3.1 TSTDPidState (PID 级状态)
| 变量名 | 类型 | 说明 |
| :--- | :--- | :--- |
| `next_send_stc` | `int64_t` | 锁相环准入时间戳 (27MHz) |
| `token_rem` | `double` | 令牌桶微比特补偿余数 |
| `err_acc_v3` | `double` | ΣΔ 累积误差器 (无死区) |

### 3.2 TSTDContext (物理引擎)
| 变量名 | 类型 | 说明 |
| :--- | :--- | :--- |
| `stc_rem` | `int64_t` | 分数阶时钟余数累积器 |
| `mux_rate` | `int64_t` | 唯一权威发包速率基准 |
| `pkt_index` | `int64_t` | 全局物理位置计数器 (TDMA 基准) |

## 4. 验证与审计 (Metrology Alignment)
*   **30ms 窗口**: 视频包分布偏差 $\pm 1$。
*   **1s 窗口**: 波动 (Fluctuation) $< \pm 48\text{kbps}$。
*   **PCR 抖动**: 硬件级 0 抖动 ($< 100\text{ns}$)。
