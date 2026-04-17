# T-STD 确定性物理整形器核心规范 (v3.3)

## 1. 核心目标：物理层零抖动 (Physical Zero-Jitter)
本规范定义了一套基于 **分数阶时钟同步**、**带 FLL 的双环 PLL** 与 **带微死区的二阶 ΣΔ 调制** 的 T-STD 物理整形架构。目标是在 1s 滑动窗口内将视频比特率波动压制在 $\pm 10\text{kbps}$ 级别，PCR 抖动 $< 100\text{ns}$。

---

## 2. 物理层：分数阶时钟与原子槽位 (Physical Layer)

### 2.1 分数阶系统时钟 (Fractional STC)
采用 Bresenham/DDA 算法消除 $27\text{MHz}$ 时钟下的累积相位误差。
*   **步进方程**:
    每次 Slot 物理步进执行：
    $$\Delta_{bits} = 1504, \quad D = Muxrate$$
    $$N = (\Delta_{bits} \times 27,000,000) / D, \quad R = (\Delta_{bits} \times 27,000,000) \pmod D$$
    $$rem \leftarrow rem + R$$
    $$v\_stc \leftarrow v\_stc + N + (1 \text{ if } rem \ge D \text{ else } 0)$$
    $$rem \leftarrow rem \pmod D \text{ if } rem \ge D$$

### 2.2 原子槽位 (Atomic Slot)
*   每个 Slot 周期内，必须且仅产生一个 TS 包（Payload 或 NULL），严禁槽位空置或重叠，确保物理层 CBR 恒定。

---

## 3. 控制层：双环控制算法 (Control Layer)

### 3.1 双环锁相环 (PLL + FLL) —— 解决相位与频率漂移
*   **Phase Lock (PLL)**: $next\_send\_stc \leftarrow \max(next\_send\_stc, v\_stc)$ (解决欠载跳变)。
*   **Frequency Lock (FLL)**:
    - 记录实际发送间隔与理想间隔的偏差：$\Delta f = actual\_dt - ideal\_dt$。
    - 修正理想间隔：$ideal\_dt \leftarrow ideal\_dt - \alpha \times \Delta f$。
*   **目的**: 确保 $next\_send\_stc$ 长期处于稳态中心，不产生“贴边”运行。

### 3.2 ΣΔ 流量控制器 (Sigma-Delta) —— 消除量化噪声
*   **误差方程**: $err\_acc \leftarrow err\_acc + (Ideal\_Bits - Actual\_Sent\_Bits)$。
*   **微死区策略 (Micro-Deadzone)**:
    - 为消除一阶 ΣΔ 的极限环高频噪声，设置 **1个 TS包** 的死区。
    - 若 $|err\_acc| < 1504$，则暂不触发补偿。
*   **准入准则**: 当 $err\_acc \ge 1504$ 时，该 PID 获得 **强制补发权**。

---

## 4. 调度层：耦合 TDMA 模型 (Scheduling Layer)

### 4.1 全局误差约束 (Global Coupling)
为了防止 Audio 插队与 Video 补偿之间产生交叉振荡（Cross Oscillation），引入全局误差器：
*   **Global Error**: $global\_err\_acc = \sum err\_acc[pid]$。
*   **约束**: 任何 PID 的准入必须满足 $global\_err\_acc$ 的负反馈方向，防止多 PID 同时爆发。

### 4.2 影子槽位与阻尼补偿 (Shadow Slot & Damping)
*   **抢占逻辑**: 当 L0/L1 抢占了 Video 槽位，Video 的 $err\_acc$ 会增加。
*   **补偿约束**:
    - Video 的瞬时补偿速率严禁超过名义码率的 **1.2x**。
    - 每次 Slot 补偿上限为 **1 packet**，防止突发脉冲。

---

## 5. 工程实现接口 (Implementation Specs)

### 5.1 Token 闭环反馈公式
为了解决 Buffer 的长期漂移，Refill 速率必须闭环：
$$token\_rate\_fb = base\_rate - K \times (Measured\_Bitrate - Muxrate)$$
*   $K$ 为阻尼系数，修正量限制在 $\pm 2\%$。

### 5.2 核心数据结构
```c
typedef struct {
    int64_t next_send_stc;    // PLL/FLL 锁相时间点
    int64_t ideal_dt;         // FLL 修正后的理想周期
    double  err_acc;          // ΣΔ 累积误差
    double  token_rate_fb;    // 闭环 Refill 速率
} TSTDPidState;
```

---

## 6. 审计与验收标准 (Metrology)
*   **1s 波动 (Fluctuation)**: 目标 $\pm 10\text{kbps}$ (1s 滑动窗口)。
*   **质量评分 (Score)**: $Score = (Max_{1s} - Min_{1s}) + 2.0 \times \sigma_{1s}$。
*   **合格线**: $Score < 30.0$ (广播级) / $50.0$ (工业级)。
