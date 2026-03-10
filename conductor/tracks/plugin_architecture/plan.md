# Implementation Plan: Industrial-Grade Plugin Architecture (Vimeo Director's Cut)

## 🎯 核心目标 (Core Objectives)
将当前基于“流树递归 (Stream Tree Recursion)”且存在内存双重释放 (Double Free / SegFault) 隐患的插件系统，彻底升级为**“注册中心 + 扁平分发 + 就地初始化 (Flat Dispatch with In-Place Init)”**模式。

## ⚠️ 痛点 (Pain Points)
1. **Double Free**: `tsa_destroy` 时，`root_stream` 和各引擎可能多次释放同一个 PID 指针。
2. **Performance**: 递归调用 `tsa_stream_send` 深度较深，包头在每个层级重复解析。
3. **Complexity**: 无法全局预览当前 handle 究竟挂载了哪些引擎。

---

## 🛠️ 执行步骤 (Implementation Phases)

### Phase 1: 核心接口重塑 (The API Redesign) [x]
**目标文件**: `include/tsa_plugin.h`
- [x] **Step 1**: 在 `tsa_plugin_ops_t` 结构体中直接增加 `void (*on_ts)(void* self, const uint8_t* pkt);` 回调函数。*(注意：各引擎在回调内应直接读取 `h->current_res` 以避免重复解析包头)*。
- [x] **Step 2**: 在注释中明确声明 `create` 函数的 `context_buf` 必须用于“就地初始化”，插件不得自行 `calloc` 内存。
- [x] **Step 3**: 导出注册中心方法：`tsa_plugins_init_registry()`, `tsa_plugins_attach_builtin(struct tsa_handle* h)`, `tsa_plugins_destroy_all(struct tsa_handle* h)`。

### Phase 2: 注册中心实现 (The Registry) [x]
**目标文件**: `src/tsa_plugin.c`
- [x] **Step 1**: 实现一个静态的 `s_registry[MAX_TSA_PLUGINS]` 数组。
- [x] **Step 2**: 实现 `tsa_plugins_init_registry`，将 `essence_ops`, `pcr_ops`, `tr101290_ops`, `tsa_scte35_engine` 全部注册。
- [x] **Step 3**: 实现 `tsa_plugin_attach_instance`，核心逻辑是把 `h->plugins[slot].context` 作为 `context_buf` 传给插件的 `create` 函数，实现零内存碎片分配。
- [x] **Step 4**: 实现 `tsa_plugins_destroy_all`，统一调用插件的 `destroy` 方法，但**绝对不执行 free(instance)**，因为内存属于 handle。

### Phase 3: 核心分发扁平化 (The Flat Dispatch) [x]
**目标文件**: `src/tsa.c`, `include/tsa_internal.h`
- [x] **Step 1**: 从 `tsa_handle_t` 中彻底删除 `tsa_stream_t root_stream;` 成员。*(注意：本次实施为保证二进制兼容性保留了成员占位，但逻辑已切断)*。
- [x] **Step 2**: 修改 `tsa_create`，移除 `root_stream` 初始化以及所有硬编码的 `extern` 注册，替换为仅调用 `tsa_plugins_init_registry()` 和 `tsa_plugins_attach_builtin(h)`。
- [x] **Step 3**: 修改 `tsa_destroy`，将老旧的清理逻辑替换为统一的 `tsa_plugins_destroy_all(h)`。
- [x] **Step 4**: 修改 `tsa_process_packet`，移除 `tsa_stream_send`，改为极其简单的循环：`for (i=0; i<MAX; i++) if (plugin_in_use) plugin->on_ts(instance, pkt);`。
- [x] **Step 5**: **处理 Reactive Demux 残留**：彻底废弃并移除 `tsa_process_packet` 中的 `enable_reactive_pid_filter` 及 `tsa_stream_demux_check_pid` 相关代码（扁平架构下直接分发，由引擎自行判断 PID）。

### Phase 4: 引擎适配 (Engine Migration) [x]
**目标文件**: `src/tsa_engine_essence.c`, `src/tsa_engine_pcr.c`, `src/tsa_engine_tr101290.c`, `src/tsa_engine_scte35.c`
**⚠️ 纪律要求：绝对禁止修改原有的 P1.1 / PCR Jitter 等业务代码，仅修改顶部结构体和底部接口！**
- [x] **Step 1**: 从各引擎的内部上下文结构体（如 `tr101290_ctx_t`）中删除 `tsa_stream_t stream;`。
- [x] **Step 2**: 移除各引擎的 `get_stream` 接口实现。
- [x] **Step 3**: 在 `on_ts` 回调中，利用 `h->current_res` 获取已解析的包头，避免重复解析。
- [x] **Step 4**: 将 `create` 适配为利用 `context_buf` 进行就地初始化（强制零分配）。

---

### Phase 5: 测试与验证 (Validation) [x]
- [x] **Step 1**: `make clean && make debug` 确保没有结构体成员引用错误（`has_pcr` 等）。
- [x] **Step 2**: 运行 `make full-test`，确保所有 135 个测试（含 `test_bitrate_consistency` 和 `test_psi`）彻底摆脱 `SEGFAULT`，在扁平架构下 100% 通过。
