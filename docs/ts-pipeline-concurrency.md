# GayoSpace 管道并发契约定稿（TS 管道附录）v1.2（随本 PR 合并生效）

> 状态：**v1.2**——v0.9 依第 10 轮评审修订（R-59 ceiling、R-60、R-61、R-62）；v1.1 依第 11 轮评审修订（R-59 复审未闭合 → **owner CAS 串行化**；R-64 新增 → **readPos CAS-max**；R-61 论证方向修正 → **复测前 acquire fence**）；v1.2 依第 12 轮评审补实现级细节（R-65：writePos 读侧 acquire + k 来源、整块边界校验、"无锁"措辞）。契约正文落在 TS-SW §3.2/§3.3/§6（唯一事实来源，本文档只放论证、前提约束与测试矩阵，防止双源漂移）
> 日期：2026-11-19（v1.0：2026-11-20 第 10 轮；v1.1：2026-11-20 第 11 轮；v1.2：2026-11-20 第 12 轮）
> 上游依据：TS-SW v1.3 §3.2/§3.3、CODE-REVIEW 第 5 轮 R-33/R-34、第 9 轮 §3、第 10 轮 R-59~R-63、第 11 轮 R-59/R-61/R-64、第 12 轮 R-65

## 1. R-33：ChannelStore 并发方案三选一定稿

### 1.1 候选与结论

| 方案 | 内容 | 结论 |
| --- | --- | --- |
| ① 双缓冲 + `atomic<uint32_t> activeIndex` | 前后台两份存储，切换发布 | **排除**：内存 ×2（TS-SW §3.3 上界 76.8 MB → 153.6 MB，或窗长减半）；"仅窗口快照双缓冲"需每帧复制最近 10 s ≈ 320 KB × 30 fps ≈ 9.6 MB/s 纯复制带宽，恒定开销买不来正确性提升 |
| ② **`std::atomic_ref<float>`（relaxed）+ `writeIndex` release/acquire + 读侧复核重试（复测前 acquire fence，R-61 修正）** | 单份存储，每批次发布 | **定稿采用** |
| ③ 裸 seqlock + "x86-TSO 依赖 + TSan 报警"声明 | 非原子 float 复核 | **排除**：按 C++ 内存模型即 data race（UB）；DoD 2.3 不允许 TSan 常态报警的技术债 |

### 1.1a 帧级单索引（R-62 定稿，2026-11-20）

存储布局为 **frame-major（帧主序）**：每帧 `ch_count` 个 float 连续存放，全部通道写完后发布**同一个** `frameIndex`（release）——**单一索引自带跨通道同帧对齐**，杜绝"ch0 来自第 N 帧、ch1 来自第 N-1 帧"的拼接错帧。不采用每通道独立索引（无法在不加锁的情况下保证多通道索引推进一致）。内存上界不变：`capacity_frames × MAX_CHANNELS × 4 B`（按当前通道数分配，TS-SW §3.3）。

### 1.2 方案②的正确性论证（为什么 relaxed 对 float 够用）

数据结构：frame-major 环形帧数组（容量 = 存储时长 × fs **帧**）+ `std::atomic<uint64_t> writeIndex`（单调递增，**按帧**计，取模寻址，release 发布 / acquire 读取）。样本值经 `std::atomic_ref<float>`（relaxed）读写——MSVC x64 上编译为普通 MOV，8ch×1kHz 下开销可忽略。

关键不变量：**单写者顺序写，写侧在途批次只触碰 `[lastPublished, lastPublished+batch)` 帧区间**。

- 读侧 acquire 读 `writeIndex` 得 `idx0`，读窗口 `[idx0 - window, idx0)`。窗口内所有帧 < `idx0` ≤ 在途批次起点——在途写**碰不到窗口**，因此不存在"一半新值一半旧值"的混合窗口。
- 窗口内每个帧的值要么是"某已发布批次的最终值"（该批次 release 发布 writeIndex，读侧 acquire 读取后可见），要么从未被写过（未覆盖区）——relaxed 的 float 访问不产生 UB（无撕裂），可见性由索引的 release/acquire 链保证。
- **读侧复测内存序（R-61 第 11 轮修正，v1.0 论证方向反了，废止）**：正确序列 = 起始 acquire 读 `writeIndex` 得 `idx0` → 读窗口（relaxed）→ **`std::atomic_thread_fence(std::memory_order_acquire)`** → 复测 `writeIndex`。acquire 语义约束的是**其后**的操作不得上移到它之前——单独的末端 load-acquire 并不能阻止其**前**的 relaxed 窗口读被后移（v1.0"acquire 保证其之前的窗口读不会被重排到它之后"这句方向反了）。标准 seqlock 读侧写法是在窗口读与复测之间插 acquire fence：fence 禁止复测读被提前到 fence 之前，从而保证复测判定建立在完整窗口读之上。写侧 release store `frameIndex` 保证窗口数据先于索引可见。x86-TSO 上该 fence 无额外指令，弱序架构（ARM）上是必要屏障。
- 复测不变 → 快照有效；变化 → 有界重试（2 次，TS-SW §3.3 语义），仍失败跳过本渲染周期。

### 1.3 前提约束与已知降级

1. **容量口径（R-60 修订）**：环形容量 = **存储时长（默认 600 s）× fs 帧**；显示窗口 ≤ 容量（缩放到全缓冲时 window == capacity）。二者是两个参数，实现须运行期校验 `window ≤ capacity`，超限拒绝配置（错误提示），不静默截断。
2. **启动未满窗（R-60 补）**：会话开始 `idx0 < window` 时，窗口起点 clamp 到 0（取模后）——只返回已写入的帧，不产生负索引/越界；第一条用例见测试矩阵 #10。
3. **window == capacity（全缓冲缩放）时跳帧概率升高**：读全程较长、发布频繁 → 复测失败 → 重试 → 跳过渲染周期。这是设计内降级（照常写入、仅渲染不推进），不阻塞采集与落盘。
4. 单写者假设由架构保证：只有解析线程写 ChannelStore（TS-SW §3.1 线程拓扑）。

## 2. R-34：SPSC 环形缓冲契约修订

### 2.1 修订点

1. **废止"零拷贝"表述**：读侧 API 一律返回**拷贝快照**（拷贝语义，处理与缓冲解耦）——TS-SW §3.2 原文"零拷贝"与"拷贝区间数据"自相矛盾，统一为后者。
2. **owner CAS 串行化 + `readInFlight` ceiling**（R-59 第 11 轮定稿，2026-11-20）：

   **为什么 ceiling 不够（第 10 轮修订复审结论）**：v0.9 的"起点定义反了"只修了一半。写侧"load 判定空闲 → 覆盖"与读侧"发布 ceiling → 拷贝"之间存在 TOCTOU 窗口：

   ```
   t0  写侧：load readPos=R, load readInFlight=R  → 判定"读侧空闲"
   t1  读侧：load readPos=R（准备 beginBatch）
   t2  写侧：CAS readPos R→R+d 成功，开始覆盖 [R, R+d)
   t3  读侧：store readInFlight = R+k（ceiling 发布）
   t4  读侧：拷贝 [R, R+k) —— 与 t2 覆盖区间重叠
   ```

   两个独立原子上的 release/acquire 挡不住这种 Dekker 式"两侧先读后写"窗口；且缓冲满时写侧下一笔写的物理槽位 `writePos mod capacity == R`，恰是读侧拷贝起点——写侧必须在**覆盖发生前**就确定读侧不在飞。修法：引入单写者/单读者的 `owner` 原子（0=空闲，1=读侧拷贝中，2=写侧覆盖决策中），把"写侧覆盖决策"与"读侧 begin+copy"在同一原子上串行化。

   ```
   读侧 beginBatch(k_max)：                          // 请求上限 k_max，实际 k 临界区内确定（R-65.1）
     CAS owner 0→1（acq_rel）        // 失败有界自旋+让步后重试；仍失败跳过本批次
     writePos.acquire 读 → k = min(k_max, writePos − readPos)
                                     // acquire 读 writePos：字节缓冲非原子，必须与写侧 release store 构成同步关系才能见到已发布字节（x86 上"碰巧对"，弱序架构必要）
                                     // 不得拷贝未发布区间；k == 0 则立即释放 owner 返回空
     readInFlight = readPos + k      // release（ceiling 发布，临界区内）
     copy [readPos, readPos + k)
     readPos = readPos + k           // CAS-max（acq_rel）：仅当新值>当前值才成功——临界区内写侧无法推进，CAS-max 是纵深防御（R-64）
     readInFlight = readPos          // release
     owner = 0                       // release

   写侧满载需腾空间：
     CAS owner 0→2（acq_rel）        // 失败（读侧拷贝中）→ 丢弃新到字节块，rx_dropped += n，不阻塞不重试
     成功 → 此刻读侧必不在飞（readInFlight == readPos 由临界区保证）
          → CAS 推进 readPos 跳过最老未读区间，droppedBytes += 跳过量
          → readInFlight = readPos（release，恢复不变量）→ owner = 0（release）→ 再写

   写侧未满整块直写（R-65.2）：整块条件 writePos + len − capacity ≤ readPos（等价剩余空间 ≥ len）
                                     // 仅校验起始槽不够：块尾别名 = writePos + len − capacity，越过 readPos 会落进拷贝区间
                                     // 整块校验通过后写入，无需 owner；校验失败 → 按满载流程走 owner 分支
   ```

   - 写侧视角在飞判定 = **CAS owner 失败**；`readInFlight` 保留为 ceiling 发布锚点与可观测状态，`readInFlight > readPos ⟺ owner == 1`；
   - 不变量 `readPos ≤ readInFlight ≤ readPos + k`（写侧 skip 分支同步更新 `readInFlight`，不破坏下界）；
   - k 在 `beginBatch` 时确定（有界：受缓冲容量与单批上限约束），拷贝期间恒定；
   - R-59 消除：覆盖决策（CAS 0→2）与 begin+copy（CAS 0→1）互斥——写侧 t0"读到空闲"后必须在覆盖前 CAS owner，读侧先拿到则写侧必然走丢新分支；
   - R-64 消除：`readPos` 双方一律 CAS-max 推进，普通 store 会被写侧 CAS 挤回导致序号回退、已丢弃区间重新变"未读"——单调不减由 CAS-max + 临界区双重保证；
   - 写侧仍然不阻塞（拿不到 owner 就丢新计数）；读侧最多自旋一个写侧"跳过决策"瞬间。`atomic_ref<float>` 方案不受影响。
3. **满载分支（R-34 保留，分支判定改为 owner CAS 结果）**：
   - CAS owner 失败（读侧拷贝中）：写侧**丢弃新到字节块**，计 `rx_dropped`；不阻塞串口线程、不触碰 `[readPos, readInFlight)` 区间。
   - CAS owner 成功（读侧空闲）：写侧推进 `readPos` 跳过最老未读区间再覆盖，跳过量计 `droppedBytes`（PRD"丢最旧"语义保留）。
   - `droppedBytes`（跳过未读）与 `rx_dropped`（丢弃新到）**独立累加、独立展示**，丢弃原因可区分；总丢弃 = 两者之和。
4. 内存序：`owner` CAS 一律 acq_rel；`writePos` 写侧 release 写 / **读侧 acquire 读**（R-65.1）；`readPos` 双方 CAS-max 推进（acq_rel）/ acquire 读；`readInFlight` 临界区内 release 写 / acquire 读；`droppedBytes`/`rx_dropped` relaxed 独立累加。

### 2.2 丢弃语义对照（status 栏展示口径）

| 计数器 | 触发 | 含义 |
| --- | --- | --- |
| `droppedBytes` | 缓冲满 + owner CAS 成功（读侧空闲） | 写侧跳过的**最老未读**字节（丢旧） |
| `rx_dropped` | 缓冲满 + owner 被读侧持有（写侧 CAS 失败，拷贝中） | 写侧丢弃的**新到**字节（保在飞批次完整性） |
| `frames_bad` / `bytes_dropped_sync` | 协议解析失败 | 与上两者不相混（TS-SW §3.2 原有口径不变） |

## 3. 测试矩阵（`core/pipeline`，覆盖率门禁 80%）

| # | 用例 | 验证点 | 类型 |
| --- | --- | --- | --- |
| 1 | SPSC 顺序写读一致 | 写入字节序 == 读出字节序 | 单线程交错（确定性） |
| 2 | SPSC 回绕 | 写满一圈后覆盖最旧、序号取模寻址正确 | 单线程交错 |
| 3 | 满载丢最旧（读侧空闲） | `droppedBytes` 计数正确、已发布未读区间未被触碰 | 单线程交错 |
| 4 | **R-59 TOCTOU（第 11 轮验收）** | 交错序列：① 写侧 load `readPos`/`readInFlight` 判定"空闲"（t0，先读后写窗口打开）→ ② 读侧 `beginBatch`（CAS owner 0→1，ceiling 发布）→ ③ 写侧覆盖决策 CAS owner 0→2 → 断言：CAS 失败走丢新分支（`rx_dropped` 计数）、在飞区间 `[R, R+k)` 数据完整、不变量 `readPos ≤ readInFlight ≤ readPos+k` 成立。第 10 轮版注入点在 ceiling 发布之后（写侧决策必然看到在飞、必绿），测不到 t0~t2 窗口，废止 | 单线程交错 |
| 5 | in-flight 清位 | publish + clear 后恢复"丢最旧"分支 | 单线程交错 |
| 6 | ChannelStore 快照一致性 | 写 N 帧后读窗口 == 期望序列 | 单线程 |
| 7 | 复测重试 | 发布推进致复测失败 → 重试成功；连续失败超限 → 跳过（注入交错模拟） | 单线程交错 |
| 8 | 帧序号时间基准 | `frame_index` 单调递增、`t = t0 + index/fs`（R-06 口径） | 单线程 |
| 9 | 有界压力 | 1 写 1 读真线程，固定帧数（如 10^6），终态守恒：written == read + droppedBytes + rx_dropped | 压力（确定性断言） |
| 10 | 启动未满窗（R-60） | 会话开始 `idx0 < window`：窗口起点 clamp 到 0，只返回已写入帧，无负索引/越界 | 单线程 |
| 11 | 跨通道同帧对齐（R-62） | 连续写多帧后读窗口：任一采样行的 ch0..chN 均来自同一帧（frame-major 布局 + 单一 frameIndex） | 单线程 |
| 12 | `readPos` 单调性（R-64） | ① 读侧 owner==1 拷贝中注入写侧推进 `readPos` 尝试 → 断言被 owner 拒绝（走丢新分支，`readPos` 不动）；② 纵深防御：外部把 `readPos` 推到 R+d（d>k）后执行 publish（CAS-max）→ 断言 `readPos == R+d` 不回退、`readInFlight`/不变量一致 | 单线程交错 |
| 13 | R-61 内存序评审检查点 | 窗口读与复测之间必须存在 `std::atomic_thread_fence(std::memory_order_acquire)`——代码评审项 + 实现处注释标注（真线程压测无法验证指令顺序，DoD 2.3 归代码评审把关） | 评审检查点 |
| 14 | 块尾边界值（R-65.2） | 写入长度恰好使块尾别名 == `readPos`（贴住整块校验边界）：断言整块落位成功、不越界、读侧未读区间数据完整 | 单线程交错 |

## 4. 并发测试确定性（DoD 2.1 反 flaky）

- **逻辑/交错用例**：SPSC 与 ChannelStore 的读写动作暴露为可显式调用的步骤（写侧 `write()/commit()` 与覆盖决策 `tryTakeOwner()`，读侧 `beginBatch()/endBatch()`），单线程按任意交错序列驱动——owner CAS 是显式步骤，t0~t2 类交错可精确重放，不依赖真实线程时序，结果唯一。
- **压力用例**：固定总量而非固定时长；只断言终态守恒与序号单调，不断言中间时序——任何调度下结论一致，天然无 flaky。
- 禁止"跑 N 秒看运气"型用例。

## 5. 本 PR 同步范围

| 文件 | 变更 |
| --- | --- |
| `docs/TS-SW.md` | §3.2 owner CAS 串行化协议（R-59/R-64：owner acq_rel、`readPos` CAS-max、满载分支按 owner CAS 结果）；§3.3 复测内存序改"窗口读 → acquire fence → 复测"（R-61 修正）；§6 JSON stats `rx_dropped` 口径同步；版本升 v1.2 |
| `CHANGELOG.md` | 记录本变更（随合并提交补 run 链接，沿用既有做法） |
| `feature/pipeline-core`（下一步） | 按本定稿实现；R-33/R-34 随本 PR 合并关闭 |
