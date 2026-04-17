# T-STD Deterministic Multiplexer Architecture
(Carrier-Grade Specification v2.0 - Final Alignment)

---

## 1. 核心范式：从“速率控制”到“确定性时间控制”
本架构彻底废弃了基于包计数和离散积分的传统调度，转而采用 **“单一时间驱动系统”**。
*   **原则**：每一个 TS 包在物理链路上的位置，由其序号 $i$ 和系统时钟 $vSTC$ 共同决定。
*   **义务发射**：当物理时钟到达理论发射时间点，系统具有发射该包的“义务”，从而从根本上消除了长时码率偏移。

---

## 2. 系统五层闭环 (Final Converged Model)

1.  **Clock Layer (Bresenham STC)**: 提供纳秒级精准、无漂移的虚拟时间基准。
2.  **Scheduler Layer (Time-Slot Anchor)**: 使用 `next_slot_stc` 作为全局硬门槛，确保物理带宽守恒。
3.  **Controller Layer (PI Pacing)**: 使用 PI 调节律控制每个 PID 的发射密度：
    $$R_v(t) = R_{target} \times (1 + K_p e(t) + K_i \int e dt)$$
4.  **Buffer Layer (Delay Ratio)**: 以 $BufferDelay / MuxDelay$ 为反馈输入，自适应调节发送力矩。
5.  **Audit Layer (L1 Expert Auditor)**: 基于梯形积分的广播级审计，实时捕获微观 DIP。

---

## 3. 关键逻辑修正 (Critical Fixes)

### 3.1 解决 860k 稳态误差
*   **方案**：引入 PI 积分项 $K_i$。尽管 $K_i$ 被设为极小值 (0.01)，但它足以在 60s 的运行周期内消除由于物理舍入产生的系统性码率偏差，使 MEANk 完美回归 800.0k。

### 3.2 解决 60s 崩盘 (Buffer Starvation)
*   **方案**：建立 **Zone A (饥饿保护区)**。当缓冲区延迟占比 < 30% 时，控制器进入“温和制动”模式，线性下调码率至 0.8x，等待编码器供货，从物理上杜绝 FIFO 彻底抽空。

---

## 4. 结论：工业级确定性
通过 v15.0 重构，Muxer 不再是一个“转发器”，而是一个“精密节拍器”。它在微观上消灭了 PCR 抖动，在宏观上实现了总线位流的绝对平衡。
