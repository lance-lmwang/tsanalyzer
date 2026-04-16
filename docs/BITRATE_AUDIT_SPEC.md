# T-STD 码率审计规范 (基于 ETSI TR 101 290)

## 1. 核心算法说明
本项目采用的码率审计算法遵循 **ETSI TR 101 290 Clause 5.3.3 (MG-bitrate)** 定义。该方法通过滑动窗口采样，计算离散时间片内的比特数，从而获取稳定的码率均值。

*   **Measurement Window (Time Gate)**: 1000ms (1秒)。用于衡量业务长期稳定性。
*   **Measurement Interval (Slice)**: 100ms。采样精度。
*   **计算公式**:
    $R = \frac{\sum_{i=1}^{10} Bits_{100ms}}{1.0s}$

## 2. 验收标准
*   **业务合规性 (1s 窗口)**: 视频流均值偏差应小于总码率的 5%。
*   **物理层冲击 (V_DEVk)**: 应尽量满足 ±48kbps (1.3Mbps 码率下)，这符合广播级接收机对 T-STD 缓冲区 (Transport Buffer) 的输入稳定性要求。

## 3. 参数配置建议
| 参数 | 建议值 | 说明 |
| :--- | :--- | :--- |
| **VBV Buffer** | 0.9s | 保证画质的上限，配合 T-STD 调度 |
| **Muxdelay** | 0.9s | 对齐 VBV 缓冲区，提供缓冲预留 |
| **PCR Period** | 30ms | 符合广电硬件 PLL 的同步特性 |
| **Audit Window**| 1000ms | 对齐 Promax TSA 分析仪标准 |
