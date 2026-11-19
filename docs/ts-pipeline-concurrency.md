# GayoSpace 管道并发契约定稿（TS 管道附录）v0.9（草稿，待 Grayson 批准）

> 状态：**草稿 v0.9**——Grayson 批准合并后升 v1.0 定稿；本文档与 TS-SW §3.2/§3.3/§6 的同步修订同 PR 交付
> 日期：2026-11-19
> 上游依据：TS-SW v1.0 §3.2/§3.3（R-07/R-09 并发契约）、CODE-REVIEW 第 5 轮 R-33/R-34（管道 PR 验收项）、第 9 轮 §3（开工指令）
> 本文档定位：R-33/R-34 的**设计决策记录与验收矩阵**。契约正文落在 TS-SW（唯一事实来源，本文档不复述契约全文，只保留论证、前提约束与测试矩阵），防止双源漂移（AGENTS 第八节纪律）。

## 1. R-33：ChannelStore 并发方案三选一定稿

### 1.1 候选与结论

| 方案 | 内容 | 结论 |
| --- | --- | --- |
| ① 双缓冲 + `atomic<uint32_t> activeIndex` | 前后台两份存储，切换发布 | **排除**：内存 ×2（TS-SW §3.3 上界 76.8 MB → 153.6 MB，或窗长减半）；"仅窗口快照双缓冲"需每帧复制最近 10 s ≈ 320 KB × 30 fps ≈ 9.6 MB/s 纯复制带宽，恒定开销买不来正确性提升 |
| ② **`std::atomic_ref<float>`（relaxed）+ `writeIndex` release/acquire + 读侧复核重试** | 单份存储，每批次发布 | **定稿采用** |
| ③ 裸 seqlock + "x86-TSO 依赖 + TSan 报警"声明 | 非原子 float 复核 | **排除**：按 C++ 内存模型即 data race（UB）；DoD 2.3 不允许 TSan 常态报警的技术债 |

### 1.2 方案②的正确性论证（为什么 relaxed 对 float 够用）

数据结构：每通道一份环形 `float` 数组（容量 = 窗长 × fs）+ `std::atomic<uint64_t> writeIndex`（单调递增，取模寻址，release 发布 / acquire 读取）。样本值经 `std::atomic_ref<float>`（relaxed）读写——MSVC x64 上编译为普通 MOV，8ch×1kHz 下开销可忽略。

关键不变量：**单写者顺序写，写侧在途批次只触碰 `[lastPublished, lastPublished+batch)` 区间**。

- 读侧 acquire 读 `writeIndex` 得 `idx0`，读窗口 `[idx0 - window, idx0)`。窗口内所有槽位 < `idx0` ≤ 在途批次起点——在途写**碰不到窗口**，因此不存在"一半新值一半旧值"的混合窗口。
- 窗口内每个槽位的值要么是"某已发布批次的最终值"（该批次 release 发布 writeIndex，读侧 acquire 读取后可见），要么从未被写过（未覆盖区）——relaxed 的 float 访问不产生 UB（无撕裂），可见性由索引的 release/acquire 链保证。
- 读侧读完复测 `writeIndex`：不变 → 快照有效；变化 → 有界重试（2 次，TS-SW §3.3 语义），仍失败跳过本渲染周期。

### 1.3 前提约束与已知降级

1. **`window ≤ capacity` 是硬前提**（缩放到全缓冲时 window == capacity）：写侧覆盖最老槽位（位置 `writePos - capacity`）可能落入读侧窗口边缘，单个槽位读到新值而非最老值——`atomic_ref` 保证无撕裂，但值可能"跳变一个样本"。实现须 `static_assert`/运行期校验窗长 × fs ≤ capacity；超限时拒绝配置（错误提示），不静默截断。
2. **window == capacity（全缓冲缩放）时跳帧概率升高**：读全程较长、发布频繁 → 复测失败 → 重试 → 跳过渲染周期。这是设计内降级（照常写入、仅渲染不推进），不阻塞采集与落盘。
3. 单写者假设由架构保证：只有解析线程写 ChannelStore（TS-SW §3.1 线程拓扑）。

## 2. R-34：SPSC 环形缓冲契约修订

### 2.1 修订点

1. **废止"零拷贝"表述**：读侧 API 一律返回**拷贝快照**（拷贝语义，处理与缓冲解耦）——TS-SW §3.2 原文"零拷贝"与"拷贝区间数据"自相矛盾，统一为后者。
2. **in-flight 期间空间耗尽策略**（原文缺失）：
   - 读侧在飞（`readInFlight < readPos`）：写侧**丢弃新到字节块**，计 `rx_dropped`；不阻塞串口线程、不触碰在飞区间。
   - 读侧空闲（`readInFlight == readPos`）：写侧推进 `readPos` 跳过最老未读区间再覆盖，跳过量计 `droppedBytes`（PRD"丢最旧"语义保留）。
   - `droppedBytes`（跳过未读）与 `rx_dropped`（丢弃新到）**独立累加、独立展示**，丢弃原因可区分；总丢弃 = 两者之和。
3. **`readInFlight` 内存序写清**：读侧 `release` 写（批次开始置 `readPos`、结束清为 `readPos`），写侧 `acquire` 读；不变量 `readInFlight ≤ readPos`（清位与发布同值）。写侧覆盖保护边界 = `max(readPos, readInFlight)`。

### 2.2 丢弃语义对照（status 栏展示口径）

| 计数器 | 触发 | 含义 |
| --- | --- | --- |
| `droppedBytes` | 缓冲满 + 读侧空闲 | 写侧跳过的**最老未读**字节（丢旧） |
| `rx_dropped` | 缓冲满 + 读侧在飞 | 写侧丢弃的**新到**字节（保在飞批次完整性） |
| `frames_bad` / `bytes_dropped_sync` | 协议解析失败 | 与上两者不相混（TS-SW §3.2 原有口径不变） |

## 3. 测试矩阵（`core/pipeline`，覆盖率门禁 80%）

| # | 用例 | 验证点 | 类型 |
| --- | --- | --- | --- |
| 1 | SPSC 顺序写读一致 | 写入字节序 == 读出字节序 | 单线程交错（确定性） |
| 2 | SPSC 回绕 | 写满一圈后覆盖最旧、序号取模寻址正确 | 单线程交错 |
| 3 | 满载丢最旧 | `droppedBytes` 计数正确、已发布未读区间未被触碰 | 单线程交错 |
| 4 | 在飞保护 | in-flight 期间灌满 → `rx_dropped` 计数、在飞批次数据完整 | 单线程交错 |
| 5 | in-flight 清位 | 清位后恢复"丢最旧"分支 | 单线程交错 |
| 6 | ChannelStore 快照一致性 | 写 N 帧后读窗口 == 期望序列 | 单线程 |
| 7 | 复测重试 | 发布推进致复测失败 → 重试成功；连续失败超限 → 跳过（注入交错模拟） | 单线程交错 |
| 8 | 帧序号时间基准 | `frame_index` 单调递增、`t = t0 + index/fs`（R-06 口径） | 单线程 |
| 9 | 有界压力 | 1 写 1 读真线程，固定帧数（如 10^6），终态守恒：written == read + droppedBytes + rx_dropped | 压力（确定性断言） |

## 4. 并发测试确定性（DoD 2.1 反 flaky）

- **逻辑/交错用例**：SPSC 与 ChannelStore 的读写动作暴露为可显式调用的步骤（写侧 `write()/commit()`，读侧 `beginBatch()/endBatch()`），单线程按任意交错序列驱动——不依赖真实线程时序，结果唯一。
- **压力用例**：固定总量而非固定时长；只断言终态守恒与序号单调，不断言中间时序——任何调度下结论一致，天然无 flaky。
- 禁止"跑 N 秒看运气"型用例。

## 5. 本 PR 同步范围

| 文件 | 变更 |
| --- | --- |
| `docs/TS-SW.md` | §3.2 按本文 §2 定稿文本替换（删零拷贝、两分支满载、内存序）；§3.3 并发模型改方案②；§6 JSON stats 增 `rx_dropped`；版本升 v1.1 |
| `CHANGELOG.md` | 记录本变更（随合并提交补 run 链接，沿用既有做法） |
| `feature/pipeline-core`（下一步） | 按本定稿实现；R-33/R-34 随本 PR 合并关闭 |
