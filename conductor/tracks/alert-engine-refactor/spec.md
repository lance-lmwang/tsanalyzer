# Unified Alert & Debounce Engine - Director's Spec

## 1. 核心模型: 双窗口状态机 (Dual-Window FSM)
告警必须经过滞后处理，防止链路抖动导致告警风暴。
- **Raise Window ($W_{raise}$)**: 错误持续多久才报。
- **Fall Window ($W_{fall}$)**: 错误消失多久才恢复。

## 2. 告警抑制协议 (Suppression Protocol)
- **Root Cause Priority**: 物理层错误（SYNC_LOSS） > 表格层错误（PAT/PMT） > 业务层错误（Jitter/CC）。
- **Logic**: 当高优告警处于 ACTIVE 状态时，底层告警自动进入 SUSPEND 状态，不触发 Webhook 和计数。

## 3. 结构化数据 (Structured Data)
所有告警事件必须包含上下文快照：
- `alert_id`: 唯一标识。
- `timestamp`: 触发纳秒。
- `context`: { "pid": 0x11, "bitrate": 10500000, "jitter": 2.5 }。

## 4. 验证基准
- **Flapping Test**: 模拟每秒 1 次的 CC 错误，告警应保持 ACTIVE 而不是反复开关。
- **Storm Test**: 模拟断流，预期只收到 1 条 SYNC_LOSS 告警。
