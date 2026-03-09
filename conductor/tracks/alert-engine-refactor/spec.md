# Unified Alert & Debounce Engine - Comprehensive Spec (Director's Edition)

## 1. 核心目标 (Core Objectives)
- **解耦告警与业务逻辑**: 业务层只管上报“事件 (Event)”，引擎层负责“判定 (Judgment)”。
- **参数化去抖 (Dual-Window FSM)**: 消除硬编码，支持 T_rise (Set) / T_fall (Clear) 滞后处理，防止告警震荡。
- **结构化输出**: 实现一致的 Webhook/Prometheus/CLI 告警描述。

## 2. 核心模型与协议

### 2.1 双窗口状态机 (Dual-Window FSM)
告警状态转换必须遵循时间窗口判定，防止链路抖动。
- **IDLE**: 正常状态。
- **PENDING**: 发现异常，正在进行确认计数 ($T_{raise}$)。
- **ACTIVE**: 已确认异常，记录 `first_fired_ns`。
- **RECOVERING**: 异常消失，正在进行确认计数 ($T_{fall}$)。

### 2.2 告警抑制协议 (Suppression Protocol)
- **RCA (Root Cause Analysis)**: 遵循物理层 > 表格层 > 业务层。
- **Logic**: 当高优告警（如 `SYNC_LOSS`）处于 ACTIVE 时，其关联的低优告警（如 `PID_TIMEOUT`）进入 SUSPEND 状态，不触发 Webhook 通知。

## 3. 核心数据结构 (Data Structures)

### 3.1 告警项定义 `tsa_alert_def_t`
```c
typedef struct {
    tsa_event_type_t type;    // 告警类型 (CC, Jitter, etc.)
    uint32_t raise_ms;        // 判定阈值 (ms)
    uint32_t fall_ms;         // 恢复阈值 (ms)
    uint32_t priority;        // 优先级 (P0-P3)
    bool is_critical;         // 是否导致流不可用
} tsa_alert_def_t;
```

### 3.2 告警追踪器 `tsa_alert_tracker_t`
```c
typedef struct {
    uint16_t pid;
    tsa_alert_state_t state;  // IDLE, PENDING, ACTIVE, RECOVERING
    uint64_t first_seen_ns;   // 首次发现时间
    uint64_t last_seen_ns;    // 最后一次出现时间
    uint32_t occurrence_count;// 累积发生次数
    bool is_fired;            // 是否已对外触发通知
} tsa_alert_tracker_t;
```

## 4. 验证与基准 (Validation Standards)
- **Flapping Test**: 模拟每秒 1 次的 CC 错误，告警应保持 ACTIVE 而不是反复开关。
- **Storm Test**: 模拟断流，预期只收到 1 条 `SYNC_LOSS` 告警，其余被抑制。
- **Latency Check**: 告警触发延迟应在 $T_{raise} \pm 10ms$ 范围内。
