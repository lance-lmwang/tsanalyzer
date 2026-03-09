# Implementation Plan: Industrial-Grade Plugin Architecture (Vimeo Director's Cut)

## 🎯 核心目标 (Core Objectives)
将当前基于“流树递归 (Stream Tree Recursion)”且存在内存双重释放 (Double Free / SegFault) 隐患的插件系统，彻底升级为**“注册中心 + 扁平分发 + 就地初始化 (Flat Dispatch + In-Place Init)”**的工业级媒体引擎架构。

## ⚠️ 历史踩坑教训 (Lessons Learned from Previous Attempts)
1. **不要盲目使用全量覆盖 (`write_file` 全文重写)**：极易导致结构体成员名丢失（如把 `r.has_pcr` 写成 `r->has_pcr`）或删掉其他不相关的关键代码。**必须使用微创手术（局部 `replace`）！**
2. **销毁时的野指针问题**：`tsa_destroy` 销毁 handle 时，如果插件使用了 `calloc` 且内部还挂载着流节点，会导致复杂的释放时序问题。
3. **PES 释放的越界**：在销毁 `essence` 时，若强行遍历 `TS_PID_MAX` 执行 `tsa_packet_unref`，会因访问未分配或未声明的轨道导致 SegFault。必须先检查 `h->pid_seen[i]`。

---

## 🛠️ 执行步骤 (Implementation Phases)

### Phase 1: 核心接口重塑 (The API Redesign)
**目标文件**: `include/tsa_plugin.h`
- [ ] **Step 1**: 在 `tsa_plugin_ops_t` 结构体中直接增加 `void (*on_ts)(void* self, const uint8_t* pkt);` 回调函数。
- [ ] **Step 2**: 在注释中明确声明 `create` 函数的 `context_buf` 必须用于“就地初始化”，插件不得自行 `calloc` 内存。
- [ ] **Step 3**: 导出注册中心方法：`tsa_plugins_init_registry()`, `tsa_plugins_attach_builtin(struct tsa_handle* h)`, `tsa_plugins_destroy_all(struct tsa_handle* h)`。

### Phase 2: 注册中心实现 (The Registry)
**目标文件**: `src/tsa_plugin.c`
- [ ] **Step 1**: 实现一个静态的 `s_registry[MAX_TSA_PLUGINS]` 数组。
- [ ] **Step 2**: 实现 `tsa_plugins_init_registry`，将 `essence_ops`, `pcr_ops`, `tr101290_ops`, `tsa_scte35_engine` 全部注册。
- [ ] **Step 3**: 实现 `tsa_plugin_attach_instance`，核心逻辑是把 `h->plugins[slot].context` 作为 `context_buf` 传给插件的 `create` 函数，实现零内存碎片分配。
- [ ] **Step 4**: 实现 `tsa_plugins_destroy_all`，统一调用插件的 `destroy` 方法，但**绝对不执行 free(instance)**，因为内存属于 handle。

### Phase 3: 核心分发扁平化 (The Flat Dispatch)
**目标文件**: `src/tsa.c`, `include/tsa_internal.h`
- [ ] **Step 1**: 从 `tsa_handle_t` 中彻底删除 `tsa_stream_t root_stream;` 成员。
- [ ] **Step 2**: 修改 `tsa_create`，移除 `root_stream` 初始化以及所有硬编码的 `extern` 注册，替换为仅调用 `tsa_plugins_init_registry()` 和 `tsa_plugins_attach_builtin(h)`。
- [ ] **Step 3**: 修改 `tsa_destroy`，将老旧的清理逻辑替换为统一的 `tsa_plugins_destroy_all(h)`。
- [ ] **Step 4**: 修改 `tsa_process_packet`，移除 `tsa_stream_send`，改为极其简单的循环：`for (i=0; i<MAX; i++) if (plugin_in_use) plugin->on_ts(instance, pkt);`。

### Phase 4: 引擎就地初始化适配 (The Engine Adaptation)
**目标文件**: `src/tsa_engine_essence.c`, `src/tsa_engine_pcr.c`, `src/tsa_engine_tr101290.c`, `src/tsa_engine_scte35.c`
**⚠️ 纪律要求：绝对禁止修改原有的 P1.1 / PCR Jitter 等业务代码，仅修改顶部结构体和底部接口！**
- [ ] **Step 1**: 从各引擎的内部上下文结构体（如 `tr101290_ctx_t`）中删除 `tsa_stream_t stream;`。
- [ ] **Step 2**: 将所有 `create` 函数从 `calloc` 改为 `ctx = (my_ctx_t*)context_buf; memset(ctx, 0, ...);`。
- [ ] **Step 3**: 将所有 `destroy` 函数置空 `(void)self;`，因为内存已交由 handle 统一释放。（**注意**: `essence_destroy` 依然需要遍历清理 PES 引用，但必须加上 `if (h->pid_seen[i])` 的安全检查！）。
- [ ] **Step 4**: 将原有的业务入口函数名绑定到 `ops->on_ts` 上。移除各文件底部的冗余注册函数（如 `tsa_register_pcr_engine`）。

### Phase 5: 测试与验证 (Validation)
- [ ] **Step 1**: `make clean && make debug` 确保没有结构体成员引用错误（`has_pcr` 等）。
- [ ] **Step 2**: 运行 `make full-test`，确保所有 135 个测试（含 `test_bitrate_consistency` 和 `test_psi`）彻底摆脱 `SEGFAULT`，在扁平架构下 100% 通过。
