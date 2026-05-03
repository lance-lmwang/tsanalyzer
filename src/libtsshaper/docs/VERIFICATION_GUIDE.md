# libtsshaper 独立回归测试与验证指南 (VERIFICATION GUIDE)

## 1. 概述 (Overview)

`libtsshaper` 是一个独立于 FFmpeg 的 T-STD 码率平滑引擎。为了确保其在不同环境下的功能对齐与性能稳定，本指南提供了标准化的回归测试流程。

## 2. 测试工具链 (Test Tools)

测试主要依赖以下工具：
*   **`test_real_file_shaping`**: 位于 `tests/` 目录下，用于读取物理 TS 文件并应用 shaper 逻辑生成平滑后的输出。
*   **`tsanalyzer`**: 用于对输出的 TS 文件进行深度的比特率平滑度（Score）和 PCR 抖动分析。
*   **`regression_suite.sh`**: 自动化测试脚本，驱动上述工具完成全量矩阵验证。

## 3. 回归测试指标 (KPIs)

| 指标 (Metric) | 期望值 (Target) | 说明 (Description) |
| :--- | :--- | :--- |
| **Bitrate Delta** | < 88 kbps | 30-PCR 窗口内的瞬时码率波动。 |
| **Smoothness Score** | < 350 | 基于 `tsanalyzer` 的综合平滑度评分。 |
| **PCR Jitter** | < 500 ns | 系统级时钟抖动偏差。 |
| **Buffer Compliance** | PASS | 不触发 TB_n 溢出或 VBV 饥饿告警。 |

## 4. 执行回归测试 (Running Regression)

### 4.1 准备环境
确保已安装 `gcc`, `bc`, `python3` 以及 `tsanalyzer` 环境。您必须先将最新的 `libtsshaper` 库编译并集成到依赖的 FFmpeg 中。

### 4.2 编译与集成
```bash
cd src/libtsshaper
chmod +x scripts/build_ffmpeg_integration.sh
./scripts/build_ffmpeg_integration.sh
```

### 4.3 运行全量验证脚本
集成完成后，运行回归矩阵：
```bash
chmod +x scripts/tstd_regression_suite.sh
./scripts/tstd_regression_suite.sh --all
```

## 5. 手动验证流程 (Manual Audit)

如果需要针对特定 PID 进行深度审计：
1. **生成平滑流**：
   ```bash
   ./tests/test_real_file_shaping input.ts output.ts 2000000 0x21 1600000 0x22 128000
   ```
2. **比特率审计**：
   ```bash
   python3 ../../scripts/tsa_quality_audit.py output.ts --vid 0x21 --target 1600
   ```
3. **PCR 审计**：
   ```bash
   python3 ../../scripts/check_pcrs.py output.ts --pid 0x21
   ```

## 6. 注意事项 (Notes)
*   **CBR 强制性**：回归测试必须在开启 `strict_cbr` 模式下进行。
*   **Timeline 连续性**：在涉及 `voter` 逻辑修改时，需重点关注跨 DTS Wrap-around (33-bit) 边界的稳定性。
