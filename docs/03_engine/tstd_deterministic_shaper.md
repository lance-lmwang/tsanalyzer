# T-STD 确定性物理整形器核心规范 (v3.5 - Expert Refined)

## 1. 核心目标：物理层零抖动 (Physical Zero-Jitter)
本规范定义了一套基于 **Bresenham 分数阶时钟**、**双环 PLL/FLL** 与 **二阶 ΣΔ 调制** 的 T-STD 物理整形架构。目标：1s 波动 $\pm 10\text{kbps}$，PCR 抖动 $< 100\text{ns}$，审计评分 $Score < 30$。

---

## 2. 物理层：分数阶时钟与原子槽位 (Physical Layer)

### 2.1 分数阶系统时钟 (Fractional STC / DDA)
$$N = \lfloor (1504 \times 27,000,000) / Muxrate \rfloor$$
$$R = (1504 \times 27,000,000) \pmod{Muxrate}$$
*   **累进规则**: $rem \leftarrow rem + R$。若 $rem \ge Muxrate$，则 $v\_stc \leftarrow v\_stc + N + 1$ 且 $rem \leftarrow rem - Muxrate$；否则 $v\_stc \leftarrow v\_stc + N$。
*   **结论**: 硬件级时钟生成，彻底消除了 Slot 时间量化误差，长时运行 0 漂移。

### 2.2 原子槽位 (Atomic Slot)
*   物理总线强制离散化：每个 Slot 必须且仅产生一个 TS 包（Payload 或 NULL）。

---

## 3. 控制层：双环锁相与二阶调制 (Control Layer)

### 3.1 双环锁相环 (Phase + Frequency Lock)
*   **Phase Lock (PLL)**: $next\_send\_stc \leftarrow \max(next\_send\_stc, v\_stc)$ (解决欠载相位跳变)。
*   **Frequency Lock (FLL)**:
    - 记录偏差: $\Delta f = actual\_interval - ideal\_interval$
    - 频率修正: $ideal\_interval \leftarrow ideal\_interval - \alpha \times \Delta f$
*   **目的**: 防止 $next\_send\_stc$ 长期“贴边”运行导致的控制偏置。

### 3.2 二阶 ΣΔ 调制器 (Second-Order Sigma-Delta)
*   **核心公式**: $err\_acc \leftarrow err\_acc + (Ideal\_Bits - Actual\_Sent\_Bits)$。
*   **微死区 (Micro-Deadzone)**:
    - 为消除 Limit Cycle 高频噪声，引入 1 TS 包死区：
    - `if (abs(err_acc) < 1504) err_acc = 0;`
*   **准入准则**: 当 $err\_acc \ge 1504$ 时，PID 获得补偿权。

---

## 4. 调度层：全局耦合与影子补偿 (Scheduling Layer)

### 4.1 全局误差约束 (Global Coupling)
*   **原则**: PID 的 $err\_acc$ 必须受 $global\_err\_acc$ 约束。
*   **逻辑**: 任何 PID 的补发申请必须满足 $\text{sign}(err\_acc) \ne \text{sign}(global\_err\_acc)$ 的负反馈趋势，防止多 PID 交叉振荡。

### 4.2 影子槽位阻尼补偿 (Shadow Slot & Damping)
*   **补偿限速**: 影子槽位触发的补发速率严禁超过名义码率的 **1.2x**。
*   **单槽约束**: 每次物理 Slot 补偿上限为 **1 packet**，严禁产生抢占式 Burst。

---

## 5. 工程实现接口 (Implementation Specs)

### 5.1 Token 闭环动力学系统
$$token\_rate\_fb = base\_rate - K \times (Measured\_Bitrate - Target\_Bitrate)$$
*   $K$ 为阻尼系数，修正量限制在 $\pm 2\%$。

### 5.2 核心数据结构 (TSTDPidState)
```c
typedef struct {
    int64_t next_send_stc;    // PLL 锁相点
    int64_t ideal_interval;   // FLL 动态修正周期
    double  err_acc;          // 二阶 ΣΔ 累积误差 (Bits)
    double  token_rate_fb;    // 闭环 Refill 速率 (bps)
} TSTDPidState;
```

---

## 6. 审计与验收 (Metrology Alignment)
*   **指标**: $Score = (Max_{1s} - Min_{1s}) + 2.0 \times \sigma_{1s}$。
*   **结论**: 基于 v3.5 规范的实现，理论上在 Promax 审计中可实现“完美的直线”比特率表现。
