# TsAnalyzer 工业计量标准 (Industrial Metrology Standards)

为了对齐 `libltntstools` 等工业级标准并遵循 ISO/IEC 13818-1 规范，TsAnalyzer 的所有码率统计和时钟分析必须遵循以下准则：

### 1. 物理码率定义 (Total TS Bitrate)
*   **统计口径**：物理码率代表 **L2 传输流层速率**。它必须统计进入引擎的所有有效 188 字节 TS 包（含 Null Packets）。
*   **计算公式**：`Total_TS_Bitrate = (ΔPackets * 1504) / ΔWall_Clock_ns`。
*   **排除项**：严禁包含 IP/UDP/Ethernet 等网络层开销。
*   **强制原则 (ISO/IEC 13818-1)**：分析引擎必须将每个到达的包视为不可抹除的物理事实。**严禁在 TS 层执行基于 PID+CC 的去重**。重复包可能携带更新的 PCR 且真实占用链路带宽，任何人为丢弃都会导致码率失真、PCR 样本丢失及 T-STD 模拟偏差。

### 2. 时钟域隔离与上帝时间源 (Clock Domain Isolation)
*   **禁止混用**：严禁在同一个结算周期内混合使用 PCR Ticks (27MHz) 和系统纳秒 (1GHz)。这会导致分母错位产生 10 倍级误差。
*   **自采样原则**：`tsa_calc_stream_bitrate` 内部应独立调用 `CLOCK_MONOTONIC` 获取结算瞬时的时间戳，以平抑系统调度抖动。
*   **最小窗口**：物理码率结算强制执行 **500ms 最小窗口保护**，以提供稳定的“线速感”。

### 3. MPTS 多节目支持
*   **上下文隔离**：每个 PID 必须拥有独立的 `pcr_track` (或 `clock_inspector`) 上下文。严禁使用全局变量存储 PCR 时间基准，防止多节目冲突。

### 4. 平滑策略 (Smoothing)
*   **物理层**：应用强 EMA 平滑（如 20% 瞬时 / 80% 历史），提供稳定的线速展示。
*   **业务层**：应用轻量平滑或不平滑（瞬时），以真实反映 CBR/VBR 流的编码质量。

---

## English Version (Metrology & MPTS Standards)

- **Physical Bitrate Definition:** The Physical Bitrate MUST represent the **Total TS Bitrate** (Level 2). It counts every valid 188-byte TS packet (including Null packets) recognized by the sync state machine. Formula: `(Δpackets * 1504) / Δwall_clock_ns`.
- **Packet Integrity:** Analysis MUST treat the incoming stream as a physical reality. No TS-layer packet de-duplication should be performed to avoid discarding valid PCR-only packets or breaking the bitrate timing baseline.
- **Clock Domain Isolation:** For MPTS support, all PCR-related metrics (Bitrate, Jitter, Accuracy) MUST be stored within the PID's independent `pcr_track` context. Never use global variables for PCR timing baseline to avoid program collision.
- **Atomic Metrology Synchronization:** Always use atomic operations for packet counters used in bitrate calculation. The sampling window for physical bitrate MUST be enforced at a minimum of 500ms to ensure stability against OS scheduling jitter.
