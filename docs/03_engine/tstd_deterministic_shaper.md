# T-STD 确定性物理整形器核心规范 (v4.0 - AWS Live Architecture)

## 1. 核心目标：物理层零抖动 (Physical Zero-Jitter)
本规范吸收了广播级编码器（如 AWS Elemental Live / Harmonic）的核心调度架构，定义了一套基于 **分数阶时钟同步 (Fractional STC)** 与 **带余量整形 (Over-Provisioning Shaper)** 的 T-STD 物理层引擎。
目标：在 1s 滑动窗口内将视频比特率波动压制在 $4\%$ 以内（$\pm 10\text{kbps}$ 级别），PCR 抖动 $< 100\text{ns}$，严格通过 Promax 工业级审计。

---

## 2. 物理层：分数阶时钟与原子槽位 (Physical Layer)

### 2.1 分数阶系统时钟 (Fractional STC / DDA)
为了消除 $27\text{MHz}$ 时钟在非整数速率下的累积相位误差，时钟生成器采用 Bresenham 算法。
$$N = \lfloor (1504 \times 27,000,000) / Muxrate \rfloor$$
$$R = (1504 \times 27,000,000) \pmod{Muxrate}$$
*   **累进规则**: $rem \leftarrow rem + R$。若 $rem \ge Muxrate$，则 $v\_stc \leftarrow v\_stc + N + 1$ 且 $rem \leftarrow rem - Muxrate$；否则 $v\_stc \leftarrow v\_stc + N$。

### 2.2 原子槽位 (Atomic Slot)
*   **物理离散化**: 每个 Slot 周期内，调度器必须产生且仅产生一个 TS 包（Payload 或 NULL）。严禁在一个循环周期内通过 `while` 连续发射多个包，必须与虚拟物理时钟步进 1:1 绑定。

---

## 3. 控制层：带余量整形与欠载复位 (Control Layer - The AWS Approach)

### 3.1 动态填充悖论与解法 (The Dynamic Padding Paradox)
传统的严格 DDA 漏桶（Strict Leaky Bucket）会将视频死死限制在标称码率（如 800k）。由于 PES/TS 头开销以及 VBR 帧级微观突发，这会导致 FIFO 堆积，引发物理层填入大量 NULL 包，最终产生宏观上的码率波动。

### 3.2 1.2x 带余量整形 (Over-Provisioning Rate, OPR)
为了让视频流平滑且不堆积，系统采用**“拉动式”**而非**“限制式”**整形。
*   **策略**: 赋予主视频流 `1.2x` 的标称码率额度（例如 800k $\rightarrow$ 960k）。
    $$Shaping\_Rate = \min(Video\_Bitrate \times 1.2, Muxrate - Overhead\_Bandwidth)$$
*   **效果**: 视频包能被更快地平滑送出。由于赋予的额度大于编码器实际产出的数据量，视频 FIFO 会被频繁抽空。

### 3.3 欠载复位 (Underflow Reset - Anti-Burst 核心)
带余量的整形器必须搭配严苛的欠载清零机制，否则空闲时积累的“配额（Credit）”会在下一帧（如 I 帧）到来时引发灾难性的连发脉冲（Burst）。
*   **触发条件**: 当该 PID 的 FIFO 为空（可用数据不足 1 个 TS 包大小）时。
*   **复位动作**: 必须将其 ΣΔ 累积误差（$err\_acc$）强制削平。
    ```c
    if (fifo_empty) {
        if (pid->err_acc > 1.0) pid->err_acc = 1.0;
    }
    ```
*   **本质**: “有包就以 1.2x 的均匀速度发，没包就作废配额，让物理层自然填充 NULL”。这是实现完美 CBR 且避免长空洞的终极奥义。

---

## 4. 调度层：双环控制与全局耦合 (Scheduling Layer)

### 4.1 积分器与准入闸门 (Sigma-Delta Gating)
*   **额度积分**: 每个物理 Slot，各 PID 增加理想额度：
    $err\_acc \leftarrow err\_acc + (Shaping\_Rate / Muxrate)$
*   **准入条件**: 当 $err\_acc \ge 1.0$ 时，该 PID 具备发包资格。一旦发包，扣除 1.0 额度。

### 4.2 影子槽位对冲 (Shadow Slot Deduction)
*   **规则**: 当物理槽位被最高优先级的 PCR 或高优 Audio 占用时，必须从该流（尤其是携带 PCR 的视频流）的 $err\_acc$ 中扣除额度（$-1.0$）。
*   **目的**: 防止高优包发出后，视频流的积分器仍然认为自己“欠了账”而在下一个 Slot 产生连发。PCR 必须融入视频流的总体带宽积分中。

### 4.3 全局误差约束 (Global Physical Conservation)
*   **物理守恒定律**:
    $global\_err\_acc \leftarrow global\_err\_acc + 1.0$ (每进一个 Slot)
    $global\_err\_acc \leftarrow global\_err\_acc - 1.0$ (每发出任何一个物理包，含 NULL)
*   **熔断保护**: 任何 Payload 都不允许在 $global\_err\_acc < -1.0$ 时发包，防止物理总线超载。

---

## 5. 验证与审计 (Metrology Alignment)
*   **1s 波动 (Fluctuation)**: 目标 $\le 4\%$（在 800k 下为 $\pm 32\text{kbps}$）。
*   **质量评分 (Score)**: $Score = (Max_{1s} - Min_{1s}) + 2.0 \times \sigma_{1s} < 50.0$。
*   **PCR Jitter**: 得益于 Fractional STC，必须严格等于 $0\text{ns}$（相对于 27MHz 时间轴）。
