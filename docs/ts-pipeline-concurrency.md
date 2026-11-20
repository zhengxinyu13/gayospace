# GayoSpace 管道并发契约定稿（TS 管道附录）v1.0（随本 PR 合并生效）

> 状态：**v1.0**——v0.9 依据第 10 轮评审修订（R-59：readInFlight 改"批次上限先发布"；R-60：容量口径与启动窗口；R-61：复测内存序；R-62：帧级单索引）。契约正文落在 TS-SW §3.2/§3.3/§6（唯一事实来源，本文档只放论证、前提约束与测试矩阵，防止双源漂移）
> 日期：2026-11-19（v1.0：2026-11-20，第 10 轮评审修订）
> 上游依据：TS-SW v1.1 §3.2/§3.3、CODE-REVIEW 第 5 轮 R-33/R-34、第 9 轮 §3、第 10 轮 R-59~R-63

## 1. R-33：ChannelStore 并发方案三选一定稿

### 1.1 候选与结论

| 方案 | 内容 | 结论 |
| --- | --- | --- |
| ① 双缓冲 + `atomic<uint32_t> activeIndex` | 前后台两份存储，切换发布 | **排除**：内存 ×2（TS-SW §3.3 上界 76.8 MB → 153.6 MB，或窗长减半）；"仅窗口快照双缓冲"需每帧复制最近 10 s ≈ 320 KB × 30 fps ≈ 9.6 MB/s 纯复制带宽，恒定开销买不来正确性提升 |
| ② **`std::atomic_ref<float>`（relaxed）+ `writeIndex` release/acquire + 读侧复核重试** | 单份存储，每批次发布 | **定稿采用** |
| ③ 裸 seqlock + "x86-TSO 依赖 + TSan 报警"声明 | 非原子 float 复核 | **排除**：按 C++ 内存模型即 data race（UB）；DoD 2.3 不允许 TSan 常态报警的技术债 |

### 1.1a 帧级单索引（R-62 定稿，2026-11-20）

存储布局为 **frame-major（帧主序）**：每帧 `ch_count` 个 float 连续存放，全部通道写完后发布**同一个** `frameIndex`（release）——**单一索引自带跨通道同帧对齐**，杜绝"ch0 来自第 N 帧、ch1 来自第 N-1 帧"的拼接错帧。不采用每通道独立索引（无法在不加锁的情况下保证多通道索引推进一致）。内存上界不变：`capacity_frames × MAX_CHANNELS × 4 B`（按当前通道数分配，TS-SW §3.3）。

### 1.2 方案②的正确性论证（为什么 relaxed 对 float 够用）

数据结构：frame-major 环形帧数组（容量 = 存储时长 × fs **帧**）+ `std::atomic<uint64_t> writeIndex`（单调递增，**按帧**计，取模寻址，release 发布 / acquire 读取）。样本值经 `std::atomic_ref<float>`（relaxed）读写——MSVC x64 上编译为普通 MOV，8ch×1kHz 下开销可忽略。

关键不变量：**单写者顺序写，写侧在途批次只触碰 `[lastPublished, lastPublished+batch)` 帧区间**。

- 读侧 acquire 读 `writeIndex` 得 `idx0`，读窗口 `[idx0 - window, idx0)`。窗口内所有帧 < `idx0` ≤ 在途批次起点——在途写**碰不到窗口**，因此不存在"一半新值一半旧值"的混合窗口。
- 窗口内每个帧的值要么是"某已发布批次的最终值"（该批次 release 发布 writeIndex，读侧 acquire 读取后可见），要么从未被写过（未覆盖区）——relaxed 的 float 访问不产生 UB（无撕裂），可见性由索引的 release/acquire 链保证。
- **读侧复测用 load-acquire**（R-61 定稿）：起始 acquire 读 `writeIndex` → 读窗口 → **末尾再以 acquire 读一次**。两端都用 acquire 是必要的：acquire 保证其之前的窗口读不会被重排到它之后——若末端用 relaxed，复测读可能被编译器/CPU 提前到窗口读之前，误接受一个已被部分覆盖的快照。频率 1 kHz、单缓存行计数器，acquire 的代价可忽略。
- 复测不变 → 快照有效；变化 → 有界重试（2 次，TS-SW §3.3 语义），仍失败跳过本渲染周期。

### 1.3 前提约束与已知降级

1. **容量口径（R-60 修订）**：环形容量 = **存储时长（默认 600 s）× fs 帧**；显示窗口 ≤ 容量（缩放到全缓冲时 window == capacity）。二者是两个参数，实现须运行期校验 `window ≤ capacity`，超限拒绝配置（错误提示），不静默截断。
2. **启动未满窗（R-60 补）**：会话开始 `idx0 < window` 时，窗口起点 clamp 到 0（取模后）——只返回已写入的帧，不产生负索引/越界；第一条用例见测试矩阵 #10。
3. **window == capacity（全缓冲缩放）时跳帧概率升高**：读全程较长、发布频繁 → 复测失败 → 重试 → 跳过渲染周期。这是设计内降级（照常写入、仅渲染不推进），不阻塞采集与落盘。
4. 单写者假设由架构保证：只有解析线程写 ChannelStore（TS-SW §3.1 线程拓扑）。

## 2. R-34：SPSC 环形缓冲契约修订

### 2.1 修订点

1. **废止"零拷贝"表述**：读侧 API 一律返回**拷贝快照**（拷贝语义，处理与缓冲解耦）——TS-SW §3.2 原文"零拷贝"与"拷贝区间数据"自相矛盾，统一为后者。
2. **`readInFlight` = 批次末序号（ceiling），拷贝之前发布**（R-59 定稿，2026-11-20；v0.9 的"批次起点"定义反了——拷贝发生在 `readPos` 发布之前，起点值与 `readPos` 相等，写侧会误判"读侧空闲"而覆盖正在拷贝的区间，非原子字节数组上是 data race）：

   ```
   beginBatch(k): readInFlight = readPos + k        // release——写侧保护上限先发布
                  // 写侧保护边界 = max(readPos, readInFlight) = readPos + k
                  // 在飞判定 = readInFlight > readPos；空闲 = 相等
   copy        :  拷贝 [readPos, readPos + k)       // 此区间已被 ceiling 保护
   publish     :  readPos = readPos + k（release）
   clear       :  readInFlight = readPos（release）
   ```

   - 写侧每轮覆盖判定前 acquire 读 `readPos` 与 `readInFlight`，保护边界 = `max(readPos, readInFlight)`；
   - 不变量 `readPos ≤ readInFlight ≤ readPos + k`（ceiling 先于拷贝发布，后清为 `readPos`）；
   - k 在 `beginBatch` 时确定（有界：受缓冲容量与单批上限约束），拷贝期间恒定。
3. **in-flight 期间空间耗尽策略**（原文缺失）：
   - 读侧在飞（`readInFlight > readPos`）：写侧**丢弃新到字节块**，计 `rx_dropped`；不阻塞串口线程、不触碰 `[readPos, readInFlight)` 区间。
   - 读侧空闲（`readInFlight == readPos`）：写侧推进 `readPos` 跳过最老未读区间再覆盖，跳过量计 `droppedBytes`（PRD"丢最旧"语义保留）。
   - `droppedBytes`（跳过未读）与 `rx_dropped`（丢弃新到）**独立累加、独立展示**，丢弃原因可区分；总丢弃 = 两者之和。
4. 内存序：`readInFlight` 读侧 release 写 / 写侧 acquire 读；`readPos` 读侧 release 写（含写侧满载 CAS 推进，release）/ 双方 acquire 读。

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
| 3 | 满载丢最旧（读侧空闲） | `droppedBytes` 计数正确、已发布未读区间未被触碰 | 单线程交错 |
| 4 | 在飞保护（**ceiling 先发布**，R-59 验收） | `beginBatch` 后、拷贝完成前**注入一次写侧满载尝试**：① 在飞区间 `[R, R+L)` 数据完整未被触碰；② 丢弃计到 `rx_dropped` 而非 `droppedBytes`；③ 走"在飞丢弃"分支而非"丢最旧"分支。按 v0.9 协议本用例必失败——它就是为抓 R-59 而设 | 单线程交错 |
| 5 | in-flight 清位 | publish + clear 后恢复"丢最旧"分支 | 单线程交错 |
| 6 | ChannelStore 快照一致性 | 写 N 帧后读窗口 == 期望序列 | 单线程 |
| 7 | 复测重试 | 发布推进致复测失败 → 重试成功；连续失败超限 → 跳过（注入交错模拟） | 单线程交错 |
| 8 | 帧序号时间基准 | `frame_index` 单调递增、`t = t0 + index/fs`（R-06 口径） | 单线程 |
| 9 | 有界压力 | 1 写 1 读真线程，固定帧数（如 10^6），终态守恒：written == read + droppedBytes + rx_dropped | 压力（确定性断言） |
| 10 | 启动未满窗（R-60） | 会话开始 `idx0 < window`：窗口起点 clamp 到 0，只返回已写入帧，无负索引/越界 | 单线程 |
| 11 | 跨通道同帧对齐（R-62） | 连续写多帧后读窗口：任一采样行的 ch0..chN 均来自同一帧（frame-major 布局 + 单一 frameIndex） | 单线程 |

## 4. 并发测试确定性（DoD 2.1 反 flaky）

- **逻辑/交错用例**：SPSC 与 ChannelStore 的读写动作暴露为可显式调用的步骤（写侧 `write()/commit()`，读侧 `beginBatch()/endBatch()`），单线程按任意交错序列驱动——不依赖真实线程时序，结果唯一。
- **压力用例**：固定总量而非固定时长；只断言终态守恒与序号单调，不断言中间时序——任何调度下结论一致，天然无 flaky。
- 禁止"跑 N 秒看运气"型用例。

## 5. 本 PR 同步范围

| 文件 | 变更 |
| --- | --- |
| `docs/TS-SW.md` | §3.2 按本文 §2 定稿文本替换（删零拷贝、`readInFlight` ceiling 语义、满载两分支、内存序）；§3.3 并发模型改方案② + 帧级单索引；§6 JSON stats 增 `rx_dropped`；版本升 v1.1 |
| `CHANGELOG.md` | 记录本变更（随合并提交补 run 链接，沿用既有做法） |
| `feature/pipeline-core`（下一步） | 按本定稿实现；R-33/R-34 随本 PR 合并关闭 |
