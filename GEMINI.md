# Workspace Mandates

- **Strict Surgical Git Staging:** DO NOT use `git add .` or any global staging commands. Every file must be added explicitly or through directory-specific patterns (e.g., `git add src/*.c`) to prevent accidental staging of artifacts or sensitive data.
- **Language Standard:** ALL source code, technical comments, commit messages, and project documentation MUST be written in **English**. Dialogue with the user remains in Chinese as per global configuration.
- **Artifact Protection:** Rigorously verify `git status` before committing to ensure no `.ts`, `.json`, or temporary log files are included in the index.
- **Mandatory Formatting Check:** Before any `git commit`, I MUST execute `make format` followed by `git diff --exit-code` to ensure all code adheres to the project's style guidelines. Any identified formatting changes must be staged before finalizing the commit.
- **Mandatory Whitespace Check:** Before any `git commit`, I MUST execute `git diff --check` (or `git diff HEAD~1 --check` for the latest commit) to ensure there are no trailing whitespaces, conflict markers, or formatting regressions. Fix all identified issues before finalizing the commit.
- **Mandatory Change Review Protocol:** For all critical file modifications (especially those involving `write_file` or large `replace` operations), I MUST execute `git diff` and present the delta for review before finalizing the commit. This is to ensure structural integrity and prevent accidental data loss or regression.
- **Professional Logging Standard:** DO NOT use `printf`, `fprintf(stderr, ...)` or `puts` for any production code or debugging that might be committed. All logging MUST use the project's standardized logging macros (`tsa_info`, `tsa_warn`, `tsa_error`, `tsa_debug`) defined in `tsa_log.h`. Debug logs should be categorized with appropriate tags and set to the correct severity level.

# ️ TsAnalyzer 工业计量标准 (Metrology Standards)

为了对齐 `libltntstools` 等工业级标准并解决 MPTS/PCAP 环境下的统计偏差，所有码率统计必须遵循以下准则：

### 1. 物理码率定义 (Total TS Bitrate)
*   **统计口径**：物理码率代表 **L2 传输流层速率**。它统计进入引擎的所有唯一且有效的 188 字节 TS 包（含 Null Packets）。
*   **计算公式**：`Total_TS_Bitrate = (ΔUnique_Packets * 1504) / ΔWall_Clock_ns`。
*   **排除项**：严禁包含 IP/UDP/Ethernet 等网络层开销（即不加 +28 或 +4 字节）。

### 2. 引擎级去重 (De-duplication)
*   **背景**：PCAP 模式在 `lo` 回环接口或多路径捕获时会产生“影子包”（镜像），导致码率成倍虚高。
*   **强制逻辑**：统计点必须设在 `tsa_decode_packet` 之后。引擎必须通过 **PID + Continuity Counter (CC)** 对连续包进行校验，重复的镜像包不计入 `total_ts_packets`。

### 3. 时钟域隔离与上帝时间源 (Clock Domain Isolation)
*   **禁止混用**：严禁在同一个结算周期内混合使用 PCR Ticks (27MHz) 和系统纳秒 (1GHz)。这会导致分母错位产生 10 倍级误差。
*   **自采样原则**：`tsa_calc_stream_bitrate` 内部必须独立调用 `CLOCK_MONOTONIC` 获取结算瞬时的时间戳，而不依赖外部传入的、可能带有逻辑偏差的时间参数。
*   **最小窗口**：物理码率结算强制执行 **500ms 最小窗口保护**，以平抑系统调度抖动。

### 4. MPTS 多节目支持
*   **上下文隔离**：每个 PID 必须拥有独立的 `clock_inspector.br_est` 上下文。严禁使用全局变量存储 PCR 时间基准，防止多节目冲突。
*   **业务聚合**：快照摘要中的 `pcr_bitrate_bps` 定义为 **所有 PCR 关联 PID 的码率之和**，真实反映 MPTS 总有效负载。

### 5. 平滑策略 (Smoothing)
*   **物理层**：应用强 EMA 平滑（如 20% 瞬时 / 80% 历史），提供稳定的“线速感”。
*   **业务层**：应用轻量平滑或不平滑（瞬时），以真实反映 CBR 流的编码质量。

# Metrology & MPTS Standards

- **Physical Bitrate Definition:** The Physical Bitrate MUST represent the **Total TS Bitrate** (Level 2). It counts every unique 188-byte TS packet (including Null packets) recognized by the sync state machine. Formula: `(Δunique_packets * 1504) / Δwall_clock_ns`.
- **Packet De-duplication:** To handle PCAP loopback echoes, the engine MUST implement PID+CC based de-duplication. Only unique packets increment the `total_ts_packets` and `phys_stats.total_bytes` counters.
- **Clock Domain Isolation:** For MPTS support, all PCR-related metrics (Bitrate, Jitter, Accuracy) MUST be stored within the PID's `clock_inspector.br_est` context. Never use global variables for PCR timing baseline to avoid program collision.
- **Atomic Metrology Synchronization:** Always use atomic operations (`atomic_fetch_add`, `atomic_exchange`) for packet counters used in bitrate calculation. The sampling window for physical bitrate MUST be enforced at a minimum of 500ms to ensure stability against OS scheduling jitter.
- **Aggregated Business Rate:** The global `pcr_bitrate_bps` in the snapshot summary is defined as the SUM of all recognized program bitrates in an MPTS stream.
