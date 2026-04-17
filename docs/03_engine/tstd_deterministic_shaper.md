# T-STD 确定性物理整形器核心规范 (v3.2)

## 1. 核心目标：物理层零抖动 (Physical Zero-Jitter)
本规范定义了一套基于 **分数阶时钟同步** 与 **二阶 ΣΔ 调制** 的 T-STD 物理整形架构。目标是在 1s 滑动窗口内将视频比特率波动压制在 $\pm 10\text{kbps}$ 级别，并确保 PCR 抖动小于 $100\text{ns}$。

---

## 2. 物理层：分数阶时钟与原子槽位 (Physical Layer)

### 2.1 分数阶系统时钟 (Fractional STC)
为了消除 $27\text{MHz}$ 时钟在非整数速率下的累积相位误差，系统时钟必须采用分数阶累进。
*   **状态变量**:
    - $v\_stc$: 64位物理时间戳 (ticks)。
    - $rem$: 累积余数。
    - $D$: 物理带宽基准 ($Muxrate$)。
*   **步进方程**:
    每次 Slot 物理步进时执行：
    $$\Delta_{bits} = 1504$$
    $$N = (\Delta_{bits} \times 27,000,000) / D$$
    $$R = (\Delta_{bits} \times 27,000,000) \pmod D$$
    $$rem \leftarrow rem + R$$
    $$v\_stc \leftarrow v\_stc + N + (1 \text{ if } rem \ge D \text{ else } 0)$$
    $$rem \leftarrow rem \pmod D \text{ if } rem \ge D$$

### 2.2 原子槽位 (Atomic Slot)
*   **定义**: 物理链路上发送一个 TS 包的最小不可分割时间单位。
*   **约束**: 每个 Slot 周期内，调度器必须产生且仅产生一个 TS 包（数据或 NULL），严禁槽位空置或重叠。

---

## 3. 控制层：双环控制算法 (Control Layer)

### 3.1 锁相环 Pacer (Soft-PLL) —— 解决相位漂移
用于维持每个 PID 的物理准入节拍，防止输入抖动透传。
*   **状态维护**: `next_send_stc[pid]`。
*   **步进规则**:
    - **成功发送**: $next\_send\_stc \leftarrow next\_send\_stc + \frac{TS\_BITS \times 27,000,000}{Bitrate_{pid}}$。
    - **数据欠载 (Underflow)**: 若 FIFO 为空，执行软锁相：$next\_send_stc \leftarrow \max(next\_send\_stc, v\_stc)$。
*   **效果**: 消除“报复性突发”，数据恢复后立即回归物理均匀分布。

### 3.2 ΣΔ 流量控制器 (Sigma-Delta) —— 解决量化震荡
通过累积误差反馈，实现对 Refill 速率的比特级精确控制。
*   **误差方程**:
    $$err\_acc \leftarrow err\_acc + (Ideal\_Target\_Bits - Actual\_Sent\_Bits)$$
*   **准入准则 (No Deadzone)**:
    - 只要 $err\_acc \ge 0$，该 PID 在当前槽位获得 **准入权**。
    - 严禁引入任何死区（Deadzone），确保微小误差在下一个可用槽位立即对冲。

---

## 4. 调度层：TDMA 优先级模型 (Scheduling Layer)

### 4.1 确定性优先级队列
在每个 Slot，按以下优先级执行确定性选择：
1.  **L0 (PCR/PSI)**: 物理索引强占。`packet_index % interval == 0`。
2.  **Level 1 (Audio)**: 异步优先级。受 Token 约束，允许抢占 Video 槽位。
3.  **Level 2 (Video)**: 确定性整形流。必须同时满足 `v_stc >= next_send_stc` 且 `err_acc >= 0`。
4.  **Level 3 (NULL)**: 结构化填充。

### 4.2 影子槽位补偿 (Shadow Slot Compensation)
当 L0 或 L1 占用了一个原本属于 Video 的物理槽位时，Video 的 ΣΔ 控制器会因为“未发送”而积累正向误差。Video 引擎必须在下一个非抢占槽位立即补回，以维持 1s 窗口内的总位宽守恒。

---

## 5. 工程实现接口 (Implementation Specs)

### 5.1 核心数据结构
```c
typedef struct {
    int64_t next_send_stc;    // PLL 锁相时间戳
    double  token_bits;       // 物理层令牌 (Leaky Bucket)
    double  err_acc;          // ΣΔ 累积误差 (Bits)
    double  token_rate_fb;    // 带反馈补偿的实时速率
} TSTDPidState;

typedef struct {
    int64_t v_stc;            // 分数阶主时钟 (Ticks)
    int64_t stc_rem;          // 分数阶余数
    int64_t pkt_index;        // 物理包序计数器
} TSTDContext;
```

### 5.2 反馈环动力学约束
*   **Feedback周期**: 建议每 100ms 执行一次 `token_rate_fb` 修正。
*   **阻尼系数**: 反馈修正量不得超过基准速率的 $\pm 2\%$，防止引入人工低频振荡。

---

## 6. 审计与验收标准 (Metrology)
*   **上帝视角 (God-View)**: 审计工具必须基于物理槽位仿真。
*   **波动评分 (Score)**: $Score = (Max_{1s} - Min_{1s}) + 2.0 \times \sigma_{1s}$。
*   **验收线**:
    - 广播级：$Score < 30.0$。
    - 工业级：$Score < 50.0$。
