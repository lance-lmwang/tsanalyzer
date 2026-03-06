# TsAnalyzer Pro: 维护守则与开发红线 (Maintenance Guardrails)

本手册旨在防止开发或 AI Agent 在维护过程中引入低阶错误（如误删功能、配置隔离导致 No Data 等）。所有操作必须遵守以下红线。

---

## 🟥 开发红线 (Development Redlines)

### 1. 禁止全文重写，坚持“外科手术式”修改
*   **现象**：为了修改一个参数，使用 `write_file` 重写整个 200+ 行的 Python 脚本。
*   **后果**：极易导致脚本后半部分逻辑（如 Tier 4/5 逻辑）被意外截断或丢失。
*   **红线**：修改核心脚本（如 `deploy_dashboard.py`）时，**必须**使用 `replace` 工具进行精准替换。禁止在未进行版本对比的情况下覆盖文件。

### 2. 基础设施即代码 (IaC) 的强制结构验证
*   **红线**：任何对看板生成逻辑的修改，在提交前**必须**运行相应的验证脚本（如 `python3 tests/verify_scripts.py`）。
*   **验证标准**：生成的 JSON 必须包含完整的 3 个 Plane 和 7 个 Tier 的监控维度。

---

## 🟧 运维红线 (Operational Redlines)

### 1. 彻底消除“网络隔离幻觉”
*   **背景**：在 Linux Docker 环境下，`host.docker.internal` 或局域网 IP 经常因为防火墙或路由失效。
*   **红线**：生产环境监控组件（Prometheus/Grafana）**必须使用 `network_mode: host`**。这确保了组件直接通过 `localhost` 通信，消除了所有网络层抽象。

### 2. PromQL 标签解耦 (De-Labeling)
*   **红线**：在 Grafana 查询语句中，除非必要，禁止硬编码 `instance` 标签。
*   **正确做法**：使用 `dominant_failure_domain{stream_id="$stream"}` 而不是带上具体的 IP 标签。这样系统在更换 IP 或迁移时不会出现 "No Data"。

---

## 🟨 诊断红线 (Diagnostic Redlines)

### 1. 拒绝“进程在跑=系统正常”的假象
*   **红线**：验证系统状态时，必须遵循 **AAT (Appliance Acceptance Test)** 数据流路径：
    1.  **探测引擎** (`curl :8088/metrics`)：确认是否有 raw 原始指标？
    2.  **探测采集** (`curl :9090/api/v1/targets`)：Prometheus 是否抓到了数据？
    3.  **探测逻辑** (`curl :9090/api/v1/query...`)：推理引擎是否算出了二次指标？
    4.  **探测展示** (`curl :3000/api/dashboards/...`)：看板配置是否正确加载？

---

## 🚀 应急恢复指令 (Emergency Command)

如果系统出现 "No Data" 或逻辑异常，严禁瞎猜，请直接执行全系统初始化与验收脚本：
```bash
bash scripts/appliance_boot_verify.sh
```
该脚本会自动完成：**环境清理 -> 防火墙打通 -> 看板重布 -> 流量注入 -> AAT 自动验收。**
