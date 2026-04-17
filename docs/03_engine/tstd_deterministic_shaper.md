# T-STD 确定性物理整形器规范（v5.0 - Rate Feedback Edition）

---

## 1. 设计目标（Design Goals）

本系统实现一个**广播级 MPEG-TS Muxer 核心调度引擎**，满足：

### 1.1 码率稳定性（核心KPI）
* 1s 滑动窗口：
  * Bitrate Fluctuation ≤ ±ε（典型 ε = 4% ~ 5%）
* 符合 ETSI TR 101 290 测量模型

### 1.2 PCR 精度
* PCR Jitter < 100ns（相对 27MHz STC）
* 绝对零抖动无漂移（Phase-locked via Fractional STC）

### 1.3 T-STD 合规
* 满足 ISO/IEC 13818-1
* 无 ES buffer overflow / underflow

### 1.4 系统属性
* Real-time / Offline 完全一致
* Deterministic（无 wall-clock 依赖，基于 vSTC 虚拟节拍推移）
* 无微观突发（No Burst Emission）

---

## 2. 总体架构（Architecture）

```text
                +----------------------+
                | Fractional STC (DDA)|
                +----------------------+
                           ↓
                +----------------------+
                |  ΣΔ Token Scheduler  |
                +----------------------+
                           ↓
                +----------------------+
                | Rate Feedback Clamp  |  ← NEW in v5.0
                +----------------------+
                           ↓
                +----------------------+
                | Output TS Packet     |
                +----------------------+
```

---

## 3. 物理层（Physical Layer）

### 3.1 Fractional STC（27MHz DDA）
用于生成无累积误差的虚拟时钟：
$$N = \lfloor (1504 \times 27,000,000) / MuxRate \rfloor$$
$$R = (1504 \times 27,000,000) \pmod{MuxRate}$$

**更新规则**：
```c
rem += R;
if (rem >= muxrate) {
    v_stc += N + 1;
    rem -= muxrate;
} else {
    v_stc += N;
}
```

### 3.2 原子槽位 (Atomic Slot) & 物理间距守护 (Gap Guard)
* 每个 slot 必须输出且仅输出 1 个 TS packet（payload 或 NULL），严禁 `while` 连续连发。
* **Expert Pacing Guard (实现评审吸收)**：为了确保包在微观时间轴上的绝对平滑，强制实施物理槽位间距拦截。
```c
int64_t min_gap = (tstd->mux_rate + pid->shaping_rate_bps - 1) / pid->shaping_rate_bps;
if (tstd->physical_pkt_idx < pid->last_sent_idx + min_gap) continue;
```

---

## 4. 控制层（Control Layer）

### 4.1 ΣΔ 积分器（Token Model）
每个 PID：
$$err\_acc += \frac{ShapingRate}{MuxRate}$$
* **发包条件**：`if (err_acc >= 1.0) eligible = 1;`
* **积分扣减**：发包后 `err_acc -= 1.0;`

### 4.2 影子槽位对冲 (Shadow Slot Deduction - 实现评审吸收)
当物理槽位被最高优先级的 PCR 占用时，必须从该流的 $err\_acc$ 中同步扣除额度（$-1.0$），并且更新 `last_sent_idx`，防止 PCR 抢占导致的视频配额堆积和突发。

### 4.3 OPR（Over-Provisioning Rate）
* **v4.0 经验值**：为了利用 ΣΔ 模型拉动视频包，防止 FIFO 慢性堆积，实际工程代码中采用了 `1.2x` 的 OPR（即 $OPR = 1.2$）。
* **v5.0 推荐值**：结合 Rate Feedback 后，OPR 的主要作用退化为仅提供调度余量，无需用其硬抗波动，因此推荐更紧凑的 OPR $\approx 1.02 \sim 1.03$（$\epsilon = 5\%$）。

### 4.4 欠载复位（Underflow Reset - Anti-Burst 核心）
* 当可用数据不足 1 个 TS 包大小时，必须强制削平信用，防止“过剩信用”在下一帧（I帧）到来时产生报复性释放。
* **v4.0 物理上限**：`if (err_acc > 1.1) err_acc = 1.1;`（允许一次极其微小的对齐补课，但不准爆发）。
* **v5.0 更严格的限制**：`err_acc = FFMIN(err_acc, 0.5);`

---

## 5. 核心：码率反馈控制（Rate Feedback Layer - v5.0）

### 5.1 滑动窗口模型（TR 101 290 对齐）
* **主窗口**：1s
* **辅助窗口**：100ms（抑制 micro-burst）

### 5.2 数据结构与更新
```c
typedef struct {
    int64_t window_bits;
    int64_t history_bits[N];
    int64_t history_ts[N];
    int head, tail;
} RateMeter;

push(bits, ts);
while (ts_now - history_ts[tail] > WINDOW_NS) {
    window_bits -= history_bits[tail];
    tail++;
}
```

### 5.3 核心控制器（Gating）
```c
upper = target * (1 + ε);
lower = target * (1 - ε);
projected = window_bits + TS_PACKET_BITS;

if (projected > upper) return BLOCK;
if (projected < lower) return FORCE;
return NORMAL;
```

---

## 6. 调度融合（Scheduler Integration）

**新逻辑**：
```c
if (err_acc >= 1.0) {
    decision = rate_control(pid);

    if (decision == BLOCK)
        skip_video;
    else if (decision == FORCE)
        send_video;
    else
        normal EDF;
}
```

---

## 7. 全局守恒（Global Constraint - 实现评审吸收）
严格约束物理总线的发包能力，任何 Payload、NULL 还是 PCR 发射，都会消耗全局额度。
```c
global_bus_credit += 1.0;          // 每个 Slot 增加
if (send_packet)
    global_bus_credit -= 1.0;      // 发射任何包扣减

if (global_bus_credit < 0.0)       // 物理拦截
    forbid_payload;
```

---

## 8. 优先级规则（Priority Rules）

1. **PCR**：最高优先级，不受 rate clamp 限制，但参与影子对冲。
2. **Audio**：次高优先级，不允许 starvation。
3. **Video**：受 ΣΔ 与 Rate Feedback 双重控制。
4. **NULL**：最低优先级（兜底）。

---

## 9. 最终总结 (Final Insight)

该系统实现：
> 🎯 **从“被动 CBR” $\rightarrow$ “主动码率控制” 的跃迁**
>
> 传统系统：控制 FIFO。
> 本系统：控制“输出码率（时间域）”。ΣΔ 保证长期平均，Rate Feedback 保证窗口稳定。

并达到：
> 💥 **Broadcast-grade deterministic mux core**