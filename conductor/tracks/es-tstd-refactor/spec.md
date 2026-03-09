# ES & T-STD Model Refactor Spec (Enhanced Director's Edition)

## 1. 核心目标 (Core Objectives)
- **标准化 T-STD 仿真**: 严格遵循 ISO/IEC 13818-1 Annex D，实现 TB, MB, EB 的泄露算法。
- **解耦解析状态**: 引入独立的 PES 状态机，支持跨包重组。
- **GOP & AUB 识别**: 零拷贝探测 Access Unit Boundary (AUB) 与 NALU 结构。

## 2. 核心数学模型

### 2.1 T-STD 泄露速率定义
- **TB Rate ($R_{rx}$)**: $R_{rx} = 1.2 \times Bitrate_{physical}$ (或根据标准指定的固定倍率)。
- **EB Removal**: 在 $t = DTS$ 的时刻，瞬间将 $Size(AU)$ 从 EB 中移除。

### 2.2 PES 重组状态机 (PES Accumulator)
- **State**: `HUNTING` (寻头) -> `ACCUMULATING` (累加数据) -> `FINISHING` (提交 AU)。
- **Zero-Copy**: 仅记录每个 TS 载荷在 Packet Pool 中的指针和偏移，不进行数据 `memcpy`。

## 3. 核心数据结构

### 3.1 增强型 `tsa_es_track_t`
```c
typedef struct {
    uint16_t pid;
    
    /* Layer 1: PES State Machine */
    struct {
        uint8_t state;
        uint32_t current_pes_len;
        uint64_t last_pts;
        uint64_t last_dts;
    } pes;

    /* Layer 2: T-STD Buffers (Q64 Fixed-Point) */
    struct {
        int128_t tb_fill;   // Transport Buffer
        int128_t mb_fill;   // Multiplexing Buffer
        int128_t eb_fill;   // Elementary Buffer
        uint64_t last_update_vstc;
        bool sync_lost;     // 遇到 CC 错误时标记为同步丢失
    } tstd;

    /* Layer 3: GOP Analysis */
    struct {
        uint32_t gop_n;
        uint32_t idr_interval_ms;
        uint8_t last_slice_type;
    } gop;
} tsa_es_track_t;
```

## 4. 异常处理 (Error Handling)
- **CC Error**: 立即标记 `tstd.sync_lost = true`。直到下一个 `payload_unit_start_indicator` (PUSI) 且 PES 头部完整时，重置 T-STD 模型。
- **Missing DTS**: 如果 PES 中只有 PTS，则 $DTS = PTS$。
