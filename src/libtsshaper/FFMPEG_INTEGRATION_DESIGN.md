# FFMpeg Integration & Design Alignment Guide for libtsshaper

## 1. 背景与核心设计目标 (Background & Core Objectives)

`libtsshaper` 的初衷是将 FFmpeg 原生 `mpegtsenc.c` 中验证成功的**工业级 T-STD (Transport System Target Decoder) 码率控制与复用引擎**彻底剥离，形成一个 ABI 稳定、无第三方依赖、纯 C 实现的独立库。

### 1.1 架构哲学的对齐：同步状态机模型
在早期的独立库尝试中，曾试图引入基于 `pthread`、无锁队列和系统物理时钟（`CLOCK_MONOTONIC`）的异步 Pacer。然而，在复用器（Muxer）的语境下，这种设计会导致极大的不可控性和调试灾难。

因此，重构后的 `libtsshaper` 严格对齐了 FFmpeg 中的成功经验：
*   **同步字节驱动 (Synchronous Byte-Driven)**：废弃所有线程。引擎被设计为一个纯粹的同步状态机。
*   **虚拟分数时钟 (Fractional STC)**：不依赖物理时间。时钟的推进严格依赖于**输出的字节数**（`ticks_per_packet`）。这保证了在离线极速转码和实时推流时，输出码流的 PCR 精度具有绝对的一致性（零漂移）。

## 2. 核心模块与依赖剥离 (Dependency Decoupling)

为了让库完全脱离 FFmpeg 生存，我们对内部依赖进行了 1:1 的等价替换：
1.  **AVFifoBuffer $\rightarrow$ `tss_fifo_t`**：实现了一个极简的环形缓冲区，用于各个 PID 的 TS 包缓存。
2.  **av_log $\rightarrow$ `tss_log_cb`**：通过注入日志回调，将引擎内部的遥测（Telemetry）数据和调试信息传回宿主框架。
3.  **avio_write $\rightarrow$ `tss_write_cb`**：引擎完成调度决策后，不直接写文件，而是通过回调将 188 字节的 TS 包反向抛给 FFmpeg。
4.  **SI/PSI 调度 $\rightarrow$ `tss_si_cb`**：PAT/SDT 等表的周期性重传由 `libtsshaper` 根据虚拟时钟精确触发，并回调通知 FFmpeg 生成最新表数据。

## 3. FFmpeg `mpegtsenc` 集成细节 (Integration Hooks)

在 FFmpeg (`libavformat/mpegtsenc.c`) 侧，我们进行了深入的代码级 Hook：

1.  **上下文挂载**：在 `MpegTSWrite` 结构体中添加 `tsshaper_t *tstd_ctx`。
2.  **初始化注入 (`mpegts_init`)**：
    *   捕获 `mux_rate`，**关键换算**：将 FFmpeg 的 bytes/sec 乘以 8 转换为 `libtsshaper` 需要的 bps。
    *   遍历所有 AVStream，依据 `st->codecpar->bit_rate` 调用 `tsshaper_add_pid` 注册视频、音频和数据通道的令牌桶（Token Bucket）。
3.  **时间戳注入 (`mpegts_write_packet_internal`)**：
    *   在写包前，拦截 DTS（Decode Time Stamp），调用 `tsshaper_enqueue_dts` 进行时间线锚定。
4.  **物理负荷注入与驱动 (`write_packet`)**：
    *   拦截 FFmpeg 组装好的 188 字节 TS 包，调用 `tsshaper_enqueue_ts` 入队。
    *   紧接着调用 `tsshaper_drive` 驱动飞轮，触发内部 PI 控制器和 EDF 调度器运转。
5.  **安全收尾 (`mpegts_write_end`)**：
    *   调用 `tsshaper_drain`，确保缓存中的残余帧（尤其是音频等低码率流）在结束时被平滑发送，防止被粗暴截断。

## 4. CI/CD 构建与测试工作流 (Standard Dev Workflow)

FFmpeg 侧使用了定制的 Docker 编译脚本（`scripts_ci/docker_build.sh`），为了符合其标准设施，我们确立了以下开发与集成流程：

### 4.1 更新库产物
在 `tsanalyzer/src/libtsshaper/` 目录完成代码修改后，编译静态库并投递到 FFmpeg 依赖树：
```bash
make clean && make
cp libtsshaper.a /home/lmwang/dev/cae/ffmpeg.wz.master/ffdeps_img/libtsshaper/lib/
cp include/tsshaper/*.h /home/lmwang/dev/cae/ffmpeg.wz.master/ffdeps_img/libtsshaper/include/tsshaper/
```

### 4.2 FFmpeg 容器化编译
在 FFmpeg 的构建脚本中：
1. `FFM_DEPS+=" libtsshaper"`：自动解析并添加 `-I` 和 `-L` 路径。
2. `./configure --enable-libtsshaper`：显式开启宏保护。

执行编译：
```bash
cd /home/lmwang/dev/cae/ffmpeg.wz.master/
./scripts_ci/docker_build.sh
```

### 4.3 验证与测试
运行编译出的二进制文件，必须携带 `-muxrate` 以防止时钟计算异常：
```bash
LD_LIBRARY_PATH=ffdeps_img/libtsshaper/lib ./ffdeps_img/ffmpeg/bin/ffmpeg -nostdin \
  -f lavfi -i testsrc=duration=5:size=640x480:rate=25 \
  -f lavfi -i sine=frequency=1000:duration=5 \
  -c:v libx264 -c:a aac \
  -muxrate 3000k \
  -mpegts_tstd_mode 1 \
  -mpegts_tstd_debug 1 \
  -y test_tstd_metrics.ts
```

## 5. 关键排坑指南 (Troubleshooting & Pitfalls)

*   **VBR 死锁 (Hang) 问题**：
    *   **现象**：程序卡死在 `tsshaper_drain` 或不输出数据。
    *   **根因**：如果运行命令未指定 `-muxrate`，FFmpeg 内部默认 `ts->mux_rate = 1`（代表纯 VBR）。这导致 `libtsshaper` 中计算 `ticks_per_packet = (8 * 188 * 27M) / 1`，产生了一个天文数字。随后 `v_stc` 时钟步进失控，无法满足物理输出门限。
    *   **对策**：开启 T-STD 必须强制指定 CBR `muxrate`；库的 `drain` 环节必须引入严格的 `timeout` 机制（如 2.0s 强制跳出）。
*   **单位不匹配**：
    *   FFmpeg 中许多速率变量（如 `ts->mux_rate`）单位是 `Bytes/sec`，而 `libtsshaper` 基于网络传输标准，全部采用 `Bits/sec` (bps)。必须在接口层（Hook 处）做好 `* 8` 与 `/ 8` 的换算。
*   **日志输出被吃掉**：
    *   在库级别如果仅仅回调了 `FFmpeg` 的 `av_log`，可能会受限于全局 `loglevel`。关键的 Telemetry（如 `[T-STD SEC]` 报告），可以在库的 debug 模式下强制走 `fprintf(stderr)`，以保证能直接在控制台监控引擎健康度。

## 6. 当前开发状态与待办事项 (Current Status & TODOs)

如果您是接手此项目的新开发者，请从以下任务继续推进：

### 6.1 已完成 (Done)
- [x] **核心架构剥离**：已完成 `tsshaper.c`, `tss_scheduler.c`, `tss_metrics.c`, `tss_fifo.c` 的纯 C 重构，完全去除了原生 FFmpeg 结构体依赖。
- [x] **FFmpeg 挂载**：在 `feat_libtsshaper_integration` 分支的 `mpegtsenc.c` 中成功插入了库的 API。
- [x] **CI 构建打通**：通过 `scripts_ci/docker_build.sh` 验证了 `-ltsshaper` 的静态链接，能成功编译出带 T-STD 的 `ffmpeg`。
- [x] **遥测输出对齐**：实现了 `tss_metrics.c`，控制台可以正常输出每秒一次的 `[T-STD SEC]` CBR 平滑度和 PCR 抖动指标。

### 6.2 亟待解决的缺陷 (Critical Bugs to Fix)
- [ ] **VBR 兜底逻辑缺失**：当用户传入 `-mpegts_tstd_mode 1` 但不写 `-muxrate 3000k` 时，进程在 `tsshaper_drain` 阶段会无限卡死。
  * **修复建议**：在 `libtsshaper` 或 `mpegtsenc.c` 层增加防御，如果检测到 `mux_rate <= 1`（VBR），强制回退为普通复用或赋予一个合理的默认 CBR 码率计算 `ticks_per_packet`。
- [ ] **消除编译警告**：`make libtsshaper` 时，还有部分遗留的未使用的变量警告（如 `jitter_limit`, `best_fullness`, `v_wait_tok`）。请清理这些无用变量。

### 6.3 下一步功能开发 (Next Development Tasks)
- [ ] **回归与合规测试**：
    * 使用输出的 `test_tstd_metrics.ts`，将其喂给外部的 `tsanalyzer` 离线分析工具。
    * 验证其 PCR 整体抖动是否严格约束在 `< 30ns`，以及比特率在滑动窗口内的突发是否不超过 `+/- 44kbps`。
- [ ] **自动化发版集成**：
    * 为 `libtsshaper` 编写自动打包脚本 `build_release.sh`，生成跨平台的静态库包。
    * 修改 FFmpeg 的 `scripts_ci/download_dep.sh`，通过远程仓库拉取 `libtsshaper`，淘汰目前手动 `cp libtsshaper.a` 到 `ffdeps_img` 的开发期 Hack 手段。
