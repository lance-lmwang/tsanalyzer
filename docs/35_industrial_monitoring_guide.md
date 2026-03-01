# TsAnalyzer 工业级监控指南 (v2.3)

## 1. 核心架构说明
TsAnalyzer 采用**分析引擎 (DNA) + 协议分发层 (IO)** 的解耦架构。`tsa_server` 支持通过配置文件加载多路并发流，并自动处理 UDP 和 SRT 协议。

## 2. 配置文件规范 (`tsa.conf`)
服务器启动时默认读取当前目录下的 `tsa.conf`。

### 格式定义：
`<Stream-ID>  <URL>  [Options]`

### 示例配置：
```conf
# 1. UDP 组播/单播接收
CCTV-1   udp://0.0.0.0:19001

# 2. SRT 监听模式 (Listener)
SPORT-2  srt://:19002?mode=listener&latency=250

# 3. SRT 推流/拉流模式 (Caller)
CLOUD-3  srt://47.92.1.1:9000?mode=caller
```

## 3. 运行与监控
- **启动服务器**: `./build/tsa_server tsa.conf`
- **远程大屏 (Grafana)**: `http://<IP>:3000`
- **原始数据接口**: `http://<IP>:8080/metrics`

## 4. 自动化测试与验证
本项目提供全自动的 E2E 验证链路，无需人工干预即可校验引擎逻辑。

| 指令 | 说明 | 适用场景 |
| :--- | :--- | :--- |
| `make test-e2e` | 全自动脱屏验证 | 校验 4 路流的 Baseline 健康度 |
| `make test-chaos` | 自动化故障模拟 | 验证 15% 丢包时的系统报警响应 |
| `make full-test` | 确定性逻辑回归 | 验证代码修改是否破坏了 128 位精度算法 |

## 5. 故障模拟测试 (Chaos Testing)
本项目支持**非侵入式网络故障模拟**，通过 `chaos_proxy.py` 在网络层实施干扰。

### 自动化故障巡检示例：
```bash
# 模拟 STR-2 周期性 20% 丢包
while true; do
  echo '{"drop_rates": {"19002": 0.20}}' > chaos_config.json
  sleep 30
  echo '{"drop_rates": {"19002": 0.0}}' > chaos_config.json
  sleep 300
done
```

## 6. 稳定性审计
系统在压测期间记录 `stability_report.txt`。健康审计标准：
- **Bitrate**: 波动 < 2%
- **CC Error**: 必须为 0 (正常情况下)
- **Health**: 必须 > 95%
