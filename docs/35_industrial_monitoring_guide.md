# TsAnalyzer Pro: Industrial Monitoring & Metrics Alignment Guide

## 1. 架构愿景 (Vision)
实现微秒级确定性的多路 MPEG-TS 实时分析，并提供 100% 匹配广播级 NOC (Network Operations Center) 大屏的可视化指标。

## 2. 核心设计原则 (Design Principles)

### 2.1 职责解耦 (Decoupling)
*   **分析引擎 (`src/tsa.c`)**：只负责算法（RST, T-STD, RCA）。它不应该知道 Prometheus 标签的存在。
*   **指标导出器 (`src/tsa_exporter_prom.c`)**：作为“翻译官”，将引擎内部状态转换为大屏所需的 PromQL 格式。**禁止在 `tsa.c` 中硬编码 Prometheus 字符串。**

### 2.2 指标稳定性契约 (Metrics Stability Contract)
为防止 Grafana 看板出现 "No Data" 或 面板消失，导出器必须遵循：
1.  **强制占位**：关键指标（如 `tsa_sync_loss_errors`, `tsa_pat_error_count`）即使值为 0，也必须输出。
2.  **标签唯一性**：每个 `stream_id` 只能有一个 `instance` 来源。
3.  **别名机制**：为复杂的聚合面板提供别名指标（例如 `tsa_pid_bitrate_bps{type="actual"}` 必须存在）。

## 3. 高性能服务器实现 (Server Implementation)

### 3.1 零拷贝与锁隔离
*   **Worker 线程**：每个流绑定一个独立物理核心（CPU Affinity），使用 `recv` 繁忙轮询（Busy Poll）。
*   **控制平面**：HTTP 回调通过 `tsa_take_snapshot_full` 读取无锁双缓冲快照，确保监控抓取不干扰实时分析。

### 3.2 内存模型
*   **禁止动态分配**：在 HTTP 响应路径上禁止频繁 `malloc/free`。使用静态大缓冲区（Static Scratch Buffer）或 流式发送（Streaming Send）。
*   **栈保护**：严禁在回调函数栈上分配超过 8KB 的数组，防止高并发下的栈溢出崩溃。

## 4. 指标清单 (Metrics Schema)

| 区域 | 指标名 | 意义 | 备注 |
| :--- | :--- | :--- | :--- |
| **TIER 1** | `tsa_signal_lock_status` | 信号锁定 | 0=LOST, 1=LOCKED |
| **TIER 1** | `tsa_health_score` | 综合健康度 | 0-100 (Lid Rule) |
| **TIER 2** | `tsa_cc_errors_total` | 连续性计数 | Counter |
| **TIER 2** | `tsa_rst_network_seconds` | 剩余安全时间 | 预测性指标 |
| **TIER 3** | `tsa_essence_video_fps` | 视频帧率 | 精度 0.01 |
| **TIER 6** | `tsa_pid_inventory_bitrate_bps` | PID 明细 | 包含 pid 标签 |

## 5. 故障排查 (Troubleshooting)

### 5.1 看到 "2 个 Instance" 怎么办？
*   **原因**：Prometheus 抓取目标（Job）重复。
*   **对策**：检查 `prometheus.yml`，删除 `targets.json` 挂载，清理 Docker 卷。

### 5.2 看到 "No Data" 怎么办？
*   **原因**：指标名不匹配或 worker 忘记执行 `tsa_commit_snapshot`。
*   **对策**：使用 `curl -s localhost:8080/metrics` 验证原始输出是否包含该指标。

---
*Vimeo Engineering Standards - Professional Broadcast Monitoring Division*
