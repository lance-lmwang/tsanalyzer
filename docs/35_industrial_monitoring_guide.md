# TsAnalyzer 工业级监控指南 (v2.2)

## 1. 远程大屏访问
- **Grafana**: `http://<IP>:3000` (默认匿名访问)
- **Prometheus**: `http://<IP>:9090`
- **Metrics API**: `http://<IP>:8080/metrics`

## 2. 自动化稳定性测试
使用 `make test-e2d` 启动基础端到端验证。该目标会拉起 1 路模拟流并自动校验 JSON API。

## 3. 故障模拟测试 (Chaos Testing)
本项目支持**非侵入式网络故障模拟**，通过 `chaos_proxy.py` 实施干扰，无需修改 C 源码。

### 如何手动触发故障：
1. 启动全套系统: `./scripts/start_remote_monitoring.sh`
2. 在另一个终端修改 `chaos_config.json`:
   ```bash
   # 给 Stream 2 注入 10% 丢包
   echo '{"drop_rates": {"19002": 0.10}}' > chaos_config.json
   ```
3. 观察大屏，STR-2 的健康评分将立即下降。

### 如何进行自动化故障巡检：
您可以编写简单的 Bash 循环来模拟周期性故障：
```bash
#!/bin/bash
while true; do
  echo '{"drop_rates": {"19002": 0.20}}' > chaos_config.json
  sleep 30  # 故障期
  echo '{"drop_rates": {"19002": 0.0}}' > chaos_config.json
  sleep 300 # 恢复期
done
```

## 4. 稳定性审计日志
压测期间，系统会生成 `stability_report.txt`。通过以下命令可快速发现隐匿异常：
```bash
grep "ANOMALY" stability_report.txt
```
