# Unified Alert & Debounce Engine Spec

## 1. 核心目标 (Core Objectives)
- **解耦告警与业务逻辑**: 业务层只管上报“事件 (Event)”，引擎层负责“判定 (Judgment)”。
- **参数化去抖**: 消除硬编码，支持 T_rise/T_fall 配置。
- **结构化输出**: 实现一致的 Webhook/Prometheus/CLI 告警描述。

## 2. 核心数学模型 (Mathematical Model)

### 2.1 窗口去抖 (Windowed Debounce)
对于任意错误事件 $E$，定义状态函数 $S(t)$：
- 如果 $E$ 在窗口 $W_{rise}$ 内持续出现，则 $S(t) = ACTIVE$。
- 如果 $E$ 在窗口 $W_{fall}$ 内完全消失，则 $S(t) = RECOVERED$。

### 2.2 告警抑制 (Alert Suppression)
- **子父级抑制**: 如果 PAT (父级) 异常，自动抑制所有 PMT (子级) 的超时告警。
- **风暴控制**: 限制单 PID 在单位时间内的告警上报频率。

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

### 3.2 告警状态机 `tsa_alert_state_t`
```c
typedef struct {
    tsa_alert_state_t state;  // IDLE, PENDING, ACTIVE, RECOVERING
    uint64_t first_seen_ns;   // 首次发现时间
    uint64_t last_seen_ns;    // 最后一次出现时间
    uint32_t occurrence_count;// 累积发生次数
    bool is_fired;            // 是否已对外触发通知
} tsa_alert_tracker_t;
```

## 4. 验证标准 (Validation Standards)
- **Latency Check**: 告警触发延迟应在 $T_{raise} \pm 10ms$ 范围内。
- **Recovery Check**: 告警清除延迟应在 $T_{fall} \pm 10ms$ 范围内。
- **Suppression Test**: 断开物理流时，应只产生 1 个“同步丢失”告警，而不应产生上百个 PID 超时告警。
