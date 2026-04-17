# Track 029: FFmpeg T-STD Muxer 确定性重构执行计划

## 1. 核心目标
实现 1s 滑动窗口波动 < 4%，PCR 抖动 < 100ns 的广播级 MPEG-TS Muxer。

## 2. 执行进度 (Progress Log)

### 第一阶段：基础设施与观测 (DONE)
- [x] **Step 1**: 注入全量物理 Slot 诊断日志 (TSTD-SLOT)，暴露微观聚簇。
- [x] **Step 2**: 注入物理层核心变量，配置 1.2x OPR (Over-Provisioning Rate)。
- [x] **Step 3**: 实现全局物理守恒律 (Global Bandwidth Conservation)。

### 第二阶段：物理层攻坚 (DONE)
- [x] **Step 4**: 实施 PID 级 Sigma-Delta 准入闸门与影子对冲 (Shadow Deduction)。
- [x] **Step 5**: 实现物理间距守护 (Gap Guard) 与 欠载复位 (Underflow Reset)。
- [x] **Step 6**: **[关键突破]** 发现 `min_gap=2` 导致 600k 物理天花板，将其修正为 `min_gap=1`。

### 第三阶段：闭环反馈与专家审计 (DONE)
- [x] **Step 7**: 打造 L1 广播级审计器 (v7.0)，支持梯形积分与 100ms DIP 检测。
- [x] **Step 8**: 实现基于 `mux_delay` 的比例反馈控制 (Proportional Control)。
- [x] **Step 9**: 实施 **Zone A/B/C 三段线性流体模型**：
    - 0-300ms: 线性减速 (0.8x -> 1.0x) 防止饥饿。
    - 300-630ms: 1.0x 巡航。
    - 630ms+: 线性加速 (1.0x -> 1.2x) 清理积压。

### 第四阶段：决战均值对齐 (IN PROGRESS)
- [ ] **Step 10**: **[待执行]** 实施 Active Token Bank 模型，将“发包驱动”升级为“绝对时间驱动”。
- [ ] **Step 11**: 解决 120s 测试中，由于 1s 滑动窗口衰减相位差导致的 860k vs 800k 均值偏差。

## 3. 核心结论
- **现状**：物理坑（DIP）已填平，PCR 精度 0ns 锁定。
- **瓶颈**：1s 滑动窗口的计算存在 7% 的系统性正偏差（超发）。
- **对策**：需要从“事后补救式限速”转向“事前配额发放”。
