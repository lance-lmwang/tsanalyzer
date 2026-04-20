# T-STD 工业级物理流控模型 (Professional Pacing)

## 1. 物理架构设计
本引擎弃用了传统的“虚拟比特计数”反馈，采用了基于 **物理 FIFO 深度** 的闭环控制系统。

### 1.1 核心反馈信号 (The Physical Truth)
* **delay_ratio**: 视频物理 FIFO 实时深度与标称 muxdelay 的比值。
* **物理意义**: 消除模型因采样精度、PCR 抖动产生的累计误差，确保 Pacing 始终锚定在真实的硬件解码器缓冲区状态。

### 1.2 三段式死区自适应逻辑 (Three-Zone Adaptation)
1. **锁定区 (300ms - muxdelay)**: =10000$ 极高惯性。
2. **预警区 (muxdelay - muxdelay+300ms)**: =2000$ 中等惯性。
3. **排水区 (>muxdelay+300ms)**: =500$ 快速对齐。

## 2. 边界韧性优化
* **启动死锁修复**: 前 10,000 包全 PID 拨动时钟。
* **极速 DRAIN 机制**: 排水阶段绕过 Token Gate，100% 带宽清空积压。
