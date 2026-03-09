# ES & T-STD Model Refactor Spec

## 1. 核心目标 (Core Objectives)
- **解耦状态管理**: 将散落在 `tsa_handle` 中的 PID 缓冲状态封装为 `tsa_es_track_t`。
- **标准化 T-STD 仿真**: 严格遵循 ISO/IEC 13818-1 Annex D，实现 TB (Transport), MB (Multiplexing), EB (Elementary) 的实时漏桶算法。
- **GOP 深度分析**: 实现闭合/开放 GOP 识别、IDR 间隔统计及 I/P/B 帧分布建模。

## 2. 数学与逻辑模型 (Mathematical Model)

### 2.1 T-STD 漏桶算法 (Leaky Bucket)
对于每个 Buffer $B_n$，其填充量 $F(t)$ 定义为：
$$F(t) = F(t_0) + \int_{t_0}^{t} R_{in}(\tau) d\tau - \int_{t_0}^{t} R_{out}(\tau) d\tau$$
- **TB (Transport Buffer)**: 输入为 TS 负载，输出以固定速率 $R_{rx}$ 泄露。
- **EB (Elementary Buffer)**: 输入为 PES 负载，输出为瞬时从 Buffer 中移除一整帧 (Access Unit)。

### 2.2 A/V Skew & PTS Drift
- **Skew**: $Diff_{AV} = |(PTS_v - STC) - (PTS_a - STC)|$。
- **Drift**: 统计连续 $N$ 帧 PTS 与重建 STC 之间的线性漂移趋势。

## 3. 核心数据结构 (Data Structures)

### 3.1 统一帧抽象 `tsa_au_t`
```c
typedef struct {
    uint64_t pts_ns;          // 归一化后的 PTS
    uint64_t dts_ns;          // 归一化后的 DTS
    uint32_t size;            // 帧大小 (Bytes)
    uint8_t frame_type;       // I, P, B, IDR
    bool complete;            // 是否已完整接收
} tsa_au_t;
```

### 3.2 ES 追踪器 `tsa_es_track_t`
```c
typedef struct {
    uint16_t pid;
    uint8_t codec_type;       // H.264, H.265, AAC...
    
    /* Layer 1: Buffer Simulation (T-STD) */
    struct {
        int128_t tb_fill_q64; // TB 填充度 (Q64 固定点数)
        int128_t mb_fill_q64; // MB 填充度
        int128_t eb_fill_q64; // EB 填充度
        uint64_t last_leak_vstc;
    } tstd;

    /* Layer 2: GOP & Sequence */
    struct {
        uint32_t gop_n;       // GOP 长度
        uint32_t gop_ms;      // GOP 时长 (ms)
        bool is_closed;       // 是否闭合 GOP
        uint64_t last_idr_ns;
    } gop;

    /* Layer 3: Audio/Video Skew */
    int64_t pts_stc_delta_ns; // PTS 相对于 STC 的偏移
} tsa_es_track_t;
```

## 4. 验证标准 (Validation Standards)
- **Buffer Integrity**: 在恒定码率下，TB 缓冲区的波形应呈现锯齿状平稳分布。
- **GOP Accuracy**: 必须准确识别出 H.264/H.265 的 Access Unit Boundary (AUB)。
- **Zero-Copy**: 整个 PES 解析逻辑不得产生额外的内存拷贝。
