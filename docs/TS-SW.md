# GayoSpace 软件技术方案（TS-SW）v1.5（定稿）

> 状态：**定稿（共识版 v1.5）**——v1.5 变更：§3.2 增 `clear()` 契约条目（R-88）、§6 增 `rx_bytes` 组合口径（R-86），第 22 轮评审随 PR #11 修订；v1.4 协议引擎实现取舍与计数口径同步（§4.0/§4.1/§4.2，第 18/19 轮评审 R-80）；v1.1~v1.3 管道并发契约定稿（§3.2/§3.3/§6），论证与测试矩阵见 docs/ts-pipeline-concurrency.md；随该文档 PR 合入并经 Grayson 批准后生效。**批次③ 增补（2026-11-25，R-124/R-125，Grayson 拍板）：§6.1 send 契约七项 + §8.1 写路径下沉标注——版本号不升（R-116 先例：契约增补行不升版，随批次③ CHANGELOG 条目注明）**
> 日期：2026-11-16（v1.1：2026-11-19；v1.2：2026-11-20 第 11 轮；v1.3：2026-11-20 第 12 轮；v1.4：2026-11-22 第 19 轮；v1.5：2026-11-23 第 22 轮）
> 上游依据：docs/PRD.md（基础共识版 v1.0）、docs/DoD.md（定稿 v1.1）
> 本项目无硬件，无 TS-HW / HSI；本 TS 是唯一的"怎么做"事实来源。
> v0.2 变更：依据外部评审（DeepSeek，2026-11-16，Grayson 提交）补 6 处阻塞缺口（发送路径、失效模式 4 行、ChannelStore 数据模型、IProtocolParser 接口、协议热切换跨线程同步、串口重连策略）与全部重要/细节项；章节重排。
> v0.3 变更：待核项 #1/#2/#4 关闭——Qt 锁定 6.8 LTS（Grayson 2026-11-16 决策）；OpenCppCoverage 维持 v1.0 使用 + v1.1 评估迁移（选项 A，Grayson 确认）；FireWater any: 判定规则经核对与官方行为一致并回填出处。
> v1.0 定稿变更：按 Grayson 最终回填修订技术栈两行与待核清单口径；三项待核关闭，两项（com0com、Qwt spike）转入编码阶段执行。
> v1.1 变更（2026-11-19 定稿、2026-11-20 第 10 轮评审修订）：R-33/R-34 定稿——§3.2 删"零拷贝"、`readInFlight` 改批次末序号（ceiling）拷贝前先发布（R-59）、满载两分支（rx_dropped）；§3.3 并发模型定为 atomic_ref<float> 方案 + 帧级单索引（R-62）+ 复测 load-acquire（R-61）+ 存储时长术语修订（R-60）；§6 CLI stats 同步。
> v1.2 变更（2026-11-20 第 11 轮评审修订）：§3.2 SPSC 增 `owner` CAS 串行化（R-59 复审：消除"写侧覆盖决策 vs 读侧 begin+copy"TOCTOU）、`readPos` 改双方 CAS-max 推进（R-64 单调性）、满载分支判定改 owner CAS 结果；§3.3 复测内存序改"窗口读 → atomic_thread_fence(acquire) → 复测"（R-61 论证方向修正）；§6 `rx_dropped` 口径同步。
> v1.3 变更（2026-11-20 第 12 轮评审补充，R-65）：§3.2 `writePos` 补读侧 acquire 内存序与 k 来源（临界区内 k = min(k_max, writePos − readPos)）、写侧直写改**整块边界校验**（块尾别名 ≤ readPos）、"无锁"措辞改为"写侧无阻塞、读侧有界自旋"。
> v1.4 变更（2026-11-22 第 19 轮评审，R-80）：§4.0 `ParseStats.bytes_dropped_sync` 口径定稿（仅 JustFloat：缓冲超限丢最旧 + 帧前垃圾；FireWater 恒 0）、JustFloat 构造契约（`N ∉ [1,32]` 抛 `std::invalid_argument`）；§4.1 重同步第 3 条改**锚点重置重扫**（废止"半字节步进"文案）、补负载不全候选跳过不计错/首个可校验候选直接确认/有界缓冲 4×帧长且先解析后截断（R-75）/帧尾歧义自然消解；§4.2 补计数归属与上界清单（行上限 512 B、>32 字段整行拒、空行与空数据行忽略、坏字段 parse_errors）。

## 1. 技术栈总表

| 项 | 选型 | 说明 |
| --- | --- | --- |
| 语言 | C++20 | `std::span`、`std::atomic` 等现代设施；与参考项目 serial_debug_tool 同代 |
| UI 框架 | Qt 6.8 LTS（MSVC 2022 x64 工具链） | Widgets 优先于 QML；6.8 为 LTS；6.12 LTS 为最后一个支持 Win10 的版本（出处：Qt 官方邮件列表 Maurice Kalinowski 2026-09，https://lists.qt-project.org/pipermail/development/2026-September/047480.html ），列入 v2.0 + TODO-007 升级候选，不进 v1.0 基线。**LTS 补丁对商业客户开放**（同出处：LTS 5 年支持 + Extended Security Maintenance 仅商业订阅）——开源分发实际可得的 6.8 补丁范围与升级节奏预案见第 14 节待核 #6 |
| 构建 | CMake ≥ 3.21 | 前置依赖脚本化；MSVC x64 工具链 |
| 脚本引擎 | QJSEngine（**属 Qt6::Qml 模块**，打包计入 Qt6Qml.dll 数 MB） | Qt 自带零第三方依赖，仅用于协议解析（PRD F-04 边界） |
| 波形渲染 | Qwt | 已核对：Qwt License v1.0 = LGPL + 子类化/静态链接例外（出处 https://qwt.sourceforge.io/qwtlicense.html ，核对日 2026-11-16）；动态链接，列入 THIRD-PARTY-NOTICES |
| 单元测试 | Catch2 v3 | BSL-1.0 宽松许可；Qt 信号槽相关测试辅以 Qt Test（DoD 2.1 硬前置，随本 TS 定稿锁定） |
| 覆盖率 | OpenCppCoverage 0.9.9.0（CLI） | 生成 Cobertura 报告入 CI；上游仓库 2026-09 已归档（只读），CLI 在 VS 2022 构建产物上可正常工作，v1.0 不迁移；v1.1 评估迁移至 Microsoft.CodeCoverage.Console（需确认 CI 的 VS 许可层级），归档风险登记于第 14 节待核。**构建/测试期工具，License 与产品无关、不随产品分发**；THIRD-PARTY-NOTICES 分"运行时依赖"与"构建期工具"两节登记 |
| CI | GitHub Actions（windows-latest） | 构建 + 单测 + Win32 扫描 + UI 字符串检查 + 覆盖率 |
| 版本 | SemVer，编译期生成 `gayospace::kVersion` 常量（`cmake/version.h.in` → `gayospace_version.h`） | 运行时"关于"对话框与 CLI `--version` 可读出 |

## 2. 架构分层与目录结构

```
src/
├── core/                  # 无 UI、无平台 API 层
│   ├── serial/            # 串口封装：QSerialPort 包装、参数、热插拔、重连策略（第 8.3 节）
│   ├── protocol/          # 数据引擎：justfloat / firewater / script / rawdata + IProtocolParser 接口（第 4 节）
│   ├── pipeline/          # 环形缓冲、ChannelStore、丢弃计数（第 3.2/3.3 节）
│   ├── session/           # 会话配置读写（JSON）
│   └── applogic/          # 收发编排（第 8 节）、统计、日志
├── cli/                   # headless 前端：参数解析、输出格式化、退出码
├── gui/                   # Widgets 前端：主窗口、波形页、串口面板、脚本编辑器（第 5 节）
├── platform/              # 平台专属收敛层：唯一允许平台 API 的目录（第 11 节）；
│                          #   v1.0 含崩溃转储钩子（Win32 MiniDumpWriteDump 调用收敛于此，第 10.7 节）
└── app/                   # v1.0 不使用（目录预留）：未来若合流为单二进制，此处做 --cli 分流入口；
                             #   v1.0 实际为双二进制（gayospace-cli.exe + GayoSpace.exe），见 src/app/CMakeLists.txt
tests/
├── unit/                  # Catch2：core 全部
├── integration/           # 虚拟串口对捞
└── manual/                # 手工用例脚本与检查单
```

分层铁律：
1. `core/` 编译期不得依赖 `gui/`、`cli/`；反向依赖随意。
2. `core/` 仅用 Qt Core/SerialPort + C++ 标准库；平台差异一律进 `platform/`。
3. GUI 与 CLI 是 `applogic` 的两个平行前端，共享同一套会话配置与协议引擎。

## 3. 线程与数据流模型

### 3.1 线程拓扑

```
[串口 worker 线程] --QByteArray块--> [SPSC环形缓冲] --解析线程--> [IProtocolParser] --> [ChannelStore(seqlock快照)] --> [GUI 渲染定时器 30fps]
      (QSerialPort 宿主)                                      |                                        |
                                                              |                                        +--> [丢弃计数器 → 状态栏]
                                                              +--> [CLI 落盘]
[GUI 线程] --控制命令邮箱--> 解析线程（协议切换/通道数变更/脚本重载）
```

**QSerialPort 线程亲和性时序**：`QSerialPort` 实例在 worker 线程内构造（worker 对象 moveToThread 到专职线程，在其 `started` 信号槽里 `new QSerialPort(this)`），读写、readyRead、参数设置全部发生在该线程；主线程只通过队列连接的信号槽下发"打开/关闭/改参数"命令。一个端口一个 worker，杜绝跨线程访问。

### 3.2 SPSC 环形缓冲（R-07 并发契约 + R-34 定稿，2026-11-19）

- 存储：预分配连续 `std::byte` 数组（默认 4 MB，评审可调），读写位置用 `std::atomic<uint64_t>` 累计序号（单调递增，取模寻址）；**写侧无阻塞、读侧有界自旋**（R-65.3 措辞修正：owner 仅在覆盖决策瞬间持有，非 lock-free 严格含义，不作"无锁"性能承诺）。
- 传递单位：字节流。读侧按序号区间取**拷贝快照**（拷贝语义，处理与缓冲解耦；R-34：废止"零拷贝"表述）。
- **原子量与内存序**：
  - `owner`：单写者/单读者互斥原子（0=空闲，1=读侧拷贝中，2=写侧覆盖决策中；CAS 一律 acq_rel）——**把写侧"覆盖决策"与读侧"beginBatch+拷贝"串行化**（R-59 第 11 轮定稿：两个独立原子的 release/acquire 挡不住 Dekker 式"写侧先读空闲→覆盖"与"读侧发布 ceiling"的交错；缓冲满时写侧下一笔写的物理槽位恰为读侧拷贝起点，必须在覆盖前串行化，推演见 docs/ts-pipeline-concurrency.md §2.1）；
  - `writePos`：写侧独占写（release）；**读侧 acquire 读**（R-65.1：字节缓冲非原子，读侧必须与写侧 release store 构成同步关系才能看到已发布字节，弱序架构必要）；
  - `readPos`：读侧发布消费进度、写侧满载跳过时推进——**双方一律 CAS-max（acq_rel：仅当新值>当前值才成功）**，单调不减（R-64：publish 若为普通 store，写侧 CAS 推进到 R+d（d>k）后读侧会把它写回 R+k，序号回退、已丢弃区间重新变"未读"）；
  - `readInFlight`：读侧在飞批次的末序号（ceiling），owner 临界区内发布（release 写 / acquire 读）——保留为 ceiling 发布锚点与可观测状态；**写侧视角在飞判定 = CAS owner 失败**，`readInFlight > readPos ⟺ owner == 1`；不变量 `readPos ≤ readInFlight ≤ readPos + k`（写侧 skip 分支同步更新 `readInFlight`）；
  - `droppedBytes`（写侧跳过的未读字节）与 `rx_dropped`（写侧丢弃的新到字节）：relaxed 独立累加，读侧汇总显示；**两者独立计数、不相混**（R-34）。
- **读侧协议（R-59 第 11 轮定稿：owner 串行化 + ceiling）**：
  - `beginBatch(k_max)`（R-65.1：请求上限 k_max，实际 k 临界区内确定）：CAS `owner` 0→1（acq_rel；失败有界自旋+让步后重试，仍失败跳过本批次——写侧仅在覆盖决策瞬间持有）；临界区内 acquire 读 `writePos` 得 `k = min(k_max, writePos − readPos)`（不得拷贝未发布区间，k==0 立即释放 owner 返回空）；`readInFlight = readPos + k`（release）；
  - 拷贝 `[readPos, readPos + k)`；k 在 beginBatch 时确定（有界）且拷贝期间恒定；
  - `publish`：CAS-max 把 `readPos` 推进到 `readPos + k`（release；临界区内写侧无法推进，CAS-max 为纵深防御）；`cancelBatch`（放弃批次）：`readInFlight = readPos`（release）→ `owner = 0`（release）。
  - **`clear()`（R-88 新增契约条目，2026-11-23）**：协议切换/重配时清空缓冲——`readPos`/`readInFlight` 追平 `writePos`（读写序号对齐），未读字节计入 `droppedBytes`（丢"旧"口径）。**非并发安全方法**：调用前提 = 写侧与读侧均暂停（TS §4.4 暂停时序内）**且 `owner` 为 Idle**（无批次在飞，Debug 构建断言）；调用时刻为单线程，序号 store 无内存序要求。胶水层 `PipelinePump::switchParser` 是唯一调用点。
  - （v0.9/第 10 轮反例已废止：仅靠 ceiling 发布无法消除"写侧读到空闲后再覆盖"的 TOCTOU 窗口。）
- **满载策略（R-34 两分支，分支判定 = owner CAS 结果）**：
  - 写侧 CAS owner 0→2 **成功**（读侧空闲，此刻 `readInFlight == readPos` 由临界区保证）：推进 `readPos` 跳过最老未读区间再覆盖，跳过量计 `droppedBytes`（保留 PRD"丢最旧"），同步 `readInFlight = readPos`、`owner = 0`（release）；
  - 写侧 CAS owner **失败**（读侧拷贝中）：**丢弃新到字节块**，计 `rx_dropped`；不阻塞串口线程、不触碰 `[readPos, readInFlight)` 区间。
  - 写侧未满**整块**直写（R-65.2）：整块条件 `writePos + len − capacity ≤ readPos`（等价剩余空间 ≥ len）校验通过后写入，无需 owner；仅校验起始槽不够——块尾别名可能越过 `readPos` 落进拷贝区间；校验失败按满载流程走 owner 分支。
- 批量读 API 返回拷贝快照；区间为空立即返回，不阻塞。
- 深度：8ch×1kHz JustFloat 帧长 36 B（8×4+4），36 kB/s 下 4 MB ≈ **111 秒**缓冲深度，远超渲染周期。
- 设计论证与测试矩阵见 docs/ts-pipeline-concurrency.md（§2、§3）。

### 3.3 ChannelStore（核心数据模型；R-09 内存预算修订，2026-11-18）

- **通道数上限**：32 通道（v1.0 上限，编译期常量 `MAX_CHANNELS`；PRD F-05 要求 ≥8）。
- **存储类型：`float`**（协议引擎输出本就是 float，原文 double 纯属浪费——内存直接减半）。
- **内存预算表**（单份存储容量 = 通道数 × **存储时长** × 4 B；**存储时长可配**（R-60 术语修订：环形容量按存储时长计，与显示窗口区分——显示窗口 ≤ 存储时长，默认显示最近 10 s 可缩放到全缓冲），默认 600 s @1kHz）：

| 通道数 | 存储时长 600 s（默认） | 存储时长 60 s |
| --- | --- | --- |
| 8（典型） | 19.2 MB | 1.9 MB |
| 16 | 38.4 MB | 3.8 MB |
| 32（上限） | **76.8 MB（内存上界）** | 7.7 MB |

  原文"8 通道 ≈ 38 MB"按 double 误算且未覆盖 32 通道上限（double 口径实为 153.6 MB）；定稿口径：float + 按当前配置通道数分配 + 存储时长可调，上界 76.8 MB。环形缓冲**按当前配置的通道数分配，通道数变更时重建**（4.4 切换时序已覆盖）。
- **并发模型（R-33 定稿 + R-62 帧级单索引，2026-11-20；废止"双缓冲"、裸 seqlock 与每通道独立索引）**：**frame-major（帧主序）环形帧数组**——每帧 `ch_count` 个 float 连续存放，全部通道写完发布**同一个** `std::atomic<uint64_t> frameIndex`（release），单帧自带跨通道同帧对齐；样本读写经 `std::atomic_ref<float>`（relaxed）——**单写多读、无 UB、单份存储**（内存上界 76.8 MB 不变）。读侧 acquire 读 `frameIndex` → 读窗口（relaxed）→ **`std::atomic_thread_fence(std::memory_order_acquire)`** → 复测 `frameIndex`（R-61 第 11 轮修正：acquire 约束其**后**的操作不得上移，单独末端 load-acquire 不能阻止其**前**的窗口读被后移；标准 seqlock 读侧 = 窗口读 → acquire fence → 复测，fence 禁止复测读被提前到窗口读之前，保证复测判定建立在完整窗口读之上；论证见 docs/ts-pipeline-concurrency.md §1.2）：不变则快照有效，变化则有界重试 2 次，仍失败跳过本渲染周期（降级语义不变）。正确性论证、前提约束（显示窗口 ≤ 存储时长×fs 容量；启动未满窗 clamp 到 0）与降级说明见 docs/ts-pipeline-concurrency.md §1。解析线程**原地写入、不复制整块数据**；GUI/CLI 读侧无阻塞。
- **快照语义**：渲染每次取"最新完整状态"，按需回溯窗口（默认显示最近 10 秒，缩放可到全缓冲）；暂停模式下照常写入、仅渲染不推进（见 5.3）。
- **快照代价与全缓冲约束（R-67，2026-11-22 实测）**：8ch @1kHz 写侧、本机 MSVC RelWithDebInfo——窗长 10k 帧 ≈ 0.12 ms/次（20/20 成功）、60k 帧 ≈ 1.22 ms/次、**全缓冲 600k 帧 ≈ 24.34 ms/次、成功率 11/20**。30 fps 帧预算 33 ms，**渲染线程同步全缓冲快照不可行**。GUI 接线前必须决策（挂账 R-67，波形页开工前定）：① 渲染线程只请求有限窗长、全缓冲走分块/降频快照；② `readWindow` 移工作线程并接受跳帧；③ "全缓冲是低频操作"写进 GUI 验收。
- **协议差异的通道数语义**：JustFloat = 固定 N（用户配置）；FireWater = 按行字段数动态（以首行为基准，后续行字段数变化计错误）；Script = 按 `parse` 返回数组长度动态（上限 32，超限截断+计数）。通道数变化时 ChannelStore 重建列布局、波形页提示通道映射已重排。

### 3.4 时间基准与采样率（R-06 新增，2026-11-18）

- **问题**：USB 串口是批量到达的（典型 1~16 ms 一批），按到达时刻打点会让同批样本挤在同一时刻、下一批再跳变——x 轴抖动可达数十个百分点，"超调持续多久"这类 PID 关键读数直接失真。
- **fs 配置**：采样率由用户设置（字节流不携带）：GUI 波形页配置项；CLI `--rate FS`（Hz）。JustFloat 与 FireWater 均适用。
- **时间生成规则**：帧显示时间 = `t0 + frame_index / fs`（`frame_index` 为解析器产出的单调递增帧序号，绝不因到达批次重置）；`t0` 仅在两个时机校准为当前 `steady_clock`——①会话首帧；②串口空闲超时（≥100 ms 无新帧）后恢复。同一连续流内绝不重打点。
- **fs 未设置时的语义**：波形 x 轴显示"样本序号"（frame_index），**不假装时间**；CLI JSON 输出 `t_index` 字段替代 `t_ms`。宁可显式缺省，不可静默失真。
- CLI 契约联动见第 6 节（`--rate` 参数与 JSON meta `rate` 字段）。

## 4. 协议引擎

### 4.0 IProtocolParser 接口（阻塞项 #4 落实）

```cpp
struct ParseFrame {
    std::chrono::steady_clock::time_point t;   // 到达校准时间戳（仅用于 t0 校准，见 3.4；显示时间不用它）
    uint64_t frame_index;                      // 单调递增帧序号（显示时间 = t0 + index/fs，见 3.4）
    std::array<float, MAX_CHANNELS> ch;        // 仅前 ch_count 有效
    uint8_t ch_count;
};

struct ParseStats { uint64_t frames_ok, frames_bad, bytes_dropped_sync; uint64_t parse_errors; };
// parse_errors（第 17 轮评审 §2.3 新增）：FireWater 失败字段逐个计数（失败字段置 NaN、不丢整行），
// 独立于 frames_bad（坏帧/坏行）；JustFloat 恒 0。CLI stats.parse_errors 即取本字段（见 6 节）。
// bytes_dropped_sync（第 19 轮评审 R-80 口径定稿，仅 JustFloat 使用；FireWater 恒 0）：
//   ① 跨 feed 缓冲超限（4×帧长）丢最旧的字节；② 确认帧时帧头之前的垃圾字节（帧负载不计入）；
//   ③ 找不到帧尾时保留的跨块残尾不计数（它仍可能成帧）。

class IProtocolParser {
public:
    virtual ~IProtocolParser() = default;
    // 喂入到达的原始字节（顺序 = 串口到达顺序）；内部保持跨调用状态
    virtual void feed(std::span<const std::byte> bytes) = 0;
    // 取出一条已解析帧；无完整帧返回 std::nullopt。调用方循环取到空为止
    virtual std::optional<ParseFrame> pop_frame() = 0;
    // 清内部状态并重同步（协议切换/串口重开/通道数变更时由引擎调用）
    virtual void reset() = 0;
    virtual ParseStats stats() const = 0;
};
```

- 三个实现各自持有独立状态机，`feed` 只积累、`pop_frame` 只产出——单元测试无需真串口（喂标准字节序列 → 断言 pop 结果）。
- **JustFloat 的 CH_COUNT 来源**：字节流不含通道数，**由用户配置**（GUI 波形页配置项 / CLI `--channels N`，默认 4）。通道数变更路径：GUI 下发控制命令 → 解析线程暂停 → `reset()` + 清空环形缓冲 → 以新 N 重建 parser → 恢复（见 4.4）。配置错误（N 与实际不符）表现为持续 `frames_bad` 增长，状态栏提示"通道数与数据不匹配，请检查通道数设置"。**构造契约（第 18/19 轮评审 R-76/R-80）**：`N ∉ [1, 32]` 时构造函数抛 `std::invalid_argument`——调用方（CLI 参数解析 / GUI 配置项）必须先行校验，不提供静默钳制（钳制后按错误帧长解析比显式拒绝更危险）。

### 4.1 JustFloat（出处：https://www.vofa.plus/docs/learning/dataengines/justfloat/ ，核对日 2026-11-16）

```c
struct Frame {
    float ch_data[CH_COUNT];                        // 小端 IEEE754 float 数组
    unsigned char tail[4]{0x00, 0x00, 0x80, 0x7f};  // 帧尾（小端 +Inf），无帧头
};
```

- 解析策略：扫描字节流找帧尾 → 回推 `CH_COUNT×4` 字节为有效负载 → 重同步。
- **重同步状态机（R-26 修订，废止原文"连续失败 8 帧半字节步进"——JustFloat 无帧头无 CRC，帧尾扫描必然"成一帧"，不存在可计数的失败信号，原规则不可实现）**：
  1. 命中帧尾 → 回推区间做**帧距对齐校验**：本帧尾位置与上一确认帧尾的间距须为 `N×4+4` 的整数倍；
  2. 整数倍 → 确认帧，产出 `ParseFrame`；非整数倍 → 按**最老匹配优先**丢弃歧义帧并 `frames_bad` 计数（`frames_bad` 的精确定义 = 帧距非整倍数的丢弃次数），字节流继续扫描（跳过该候选首字节，不消费数据字节，后续真帧尾负载保持完整）；
  3. 连续歧义超过阈值（可配，默认 8）→ **锚点重置重扫**（v1.4，第 19 轮评审 R-80 定稿，废止 v1.3 前文案"半字节步进重扫"）：清空锚点，下一个命中的帧尾作为新锚重新做对齐校验——效果等价于按 float 宽度相位重试对齐（4 个可能相位由后续帧距校验自然裁决），实现简单且可测；此时它是**降级手段**而非触发规则本身。
- **负载不全候选（v1.4，R-80）**：帧尾命中但帧头不在缓冲内（流开头不足一帧 / 帧头已随此前确认被消费）→ 该候选永久无法重建，跳过不计错，继续找后续候选。回推偏移在流开头会下溢（无符号减法），实现必须先判帧尾位置 ≥ 帧长再做减法（第 18 轮修订实测踩坑：下溢绕过检查导致假确认 + 越界消费 UB）。
- **首个可校验候选直接确认**：无锚点时（首个完整候选）直接确认，无参照可校验（R-26"必然成一帧"）——配错通道数时表现为一次错位数据的假确认，其后持续 `frames_bad`。
- **有界缓冲（第 17 轮评审 §2.2，v1.4 补口径）**：跨 feed 缓冲上限 **4×帧长**；超限丢最旧计入 `bytes_dropped_sync`。`feed` 必须**先解析后截断**（第 18 轮评审 R-75 P0）：一次 feed 超 4 帧时，完整帧必须先产出，截断只作用于剩余无法成帧的尾巴。
- 帧尾歧义处理：负载中通道值恰为 +Inf 时字节序列与帧尾重合——并入上述对齐校验统一判定；负载内的假帧尾因负载不全被自然跳过（不计错）；真 +Inf 通道值本身在 PID 场景中即异常值，可从统计发现。
- **标准测试向量**（随单测落地）：正常帧流、残帧截断、粘帧（两帧连发）、帧尾字节在负载中重现（+Inf 构造帧）、帧尾错位后重同步、`CH_COUNT` 配错时的持续 `frames_bad`、批量 feed（≥10 帧，超缓冲上限）零丢弃、垃圾流触发缓冲上限、构造上界拒绝。

### 4.2 FireWater（出处：https://www.vofa.plus/docs/learning/dataengines/firewater/ ，核对日 2026-11-16）

- 格式：`"<any>:ch0,ch1,...,chN\n"`；`any` 与冒号可省略；`\n` 不可省略，兼容 `\r\n` 与 `\n\r`；`any` 不得为 `image`。
- **`any:` 前缀判定规则（R-25 收窄 + R-37 二次收窄，2026-11-19；字母/下划线白名单仍会误判带数字的合法前缀）**：行首至第一个 `:` 之间的片段，若**能被 `strtof` 完整解析为数字**（如 `1.0`、`2`）则不剔除，整行进入宽容解析；否则（字母/下划线/含数字的名称如 `channels`、`ch1`、`v2`、`pid1`、`#1`）视为前缀剔除。`image` 为前缀的行按官方约定整行丢弃并计数；空前缀与无冒号行按数据宽容解析。**判定依据**：`ch1:1,2,3` / `pid1:1,2` / `v2:1,2` / `#1:1,2` 的前缀都是"合法通道名"而非数据，白名单规则会把它们整行送进数据解析产出 NaN 通道；strtof 完整解析判据把"前缀 vs 数据"的分界统一到"这个片段像不像一个数"，对 `1.0:2.0,3.0`（前段是数据）与 `ch1:1,2`（前段是名字）都能正确归类。**`any` 为 `image` 的行按官方约定整行丢弃并计数**。待核 #4 spike 用例清单追加反例：`ch1:1,2,3` / `pid1:1,2` / `v2:1,2` / `#1:1,2` / `image:1,2` / `:1,2`（空前缀）/ `1.0:2.0,3.0` / 无冒号行。
- 解析策略：按行缓冲 → 前缀判定 → 逗号切分 → 逐字段 `strtof` 宽容解析（失败字段置 NaN 并计入错误计数，不丢整行）。
- **计数归属与上界（v1.4，第 19 轮评审 R-80 定稿）**：
  - **行缓冲上限 512 B**：超限整行丢弃（吞到下一个 `\n`）并 `frames_bad++`，防垃圾流无限增长（第 17 轮评审 §2.3）；
  - `image` 前缀行 → 整行丢弃并 `frames_bad++`（官方约定）；
  - 空行 → 静默忽略，不计错（采集流中常见心跳/空转）；
  - 前缀剔除后数据部分为空（如 `ch1:`）→ 忽略整行不计错，不产出 NaN 帧（第 18 轮评审 R-77）；
  - **字段数 > 32 → 整行丢弃并 `frames_bad++`**，`parse_errors` 不计该行字段（第 19 轮评审 R-82：先判上界再解析，已丢弃行的字段不参与计数）；通道数基准 = 首条有效数据行字段数，**恒 ≤ 32**，后续行字段数变化 → `frames_bad++` 不产出（§3.3）；
  - 坏字段（`strtof` 完全失败 / ERANGE 溢出）→ 置 NaN 并 `parse_errors++`，不丢整行；`strtof` 宽容取数（同字段内数字后接垃圾，如 `1.0:2.0`）不计错。
- 性能注记：行缓冲与字段扫描为线性；`feed` 分块调用不得改变结果（逐字节 feed 与整块 feed 逐字段一致，随单测落地）。

### 4.3 Script（自定义 JS 协议）

脚本契约（QJSEngine 全局函数，用户实现）：

```js
// 输入：bytes 为本次到达的原始字节（Uint8Array，跨调用保持状态由用户负责）
// 输出：本次解析出的通道数值数组；无完整帧返回空数组
function parse(bytes) { ... }
// 可选：清状态（协议切换/串口重开时由引擎调用）
function reset() { ... }
```

- 引擎侧保证：喂入顺序与串口到达顺序一致；错误即捕获、不崩溃；单次执行超时 100ms 强制中断（可配）。**超时中断的实现前提（R-14 spike，编码阶段验证）**：`QJSEngine::setInterrupted()` 仅在解释器安全点生效，且 Qt 未承诺跨线程调用安全——spike 必须实测 `while(true){}` 死循环能否被**解析线程内**的定时器中断；不可靠则降级方案二选一：独立进程执行脚本 + 超时 kill，或仅记录告警不强制中断（需 Grayson 确认取舍）。
- **信任边界**（R-14）：QJSEngine 无安全沙箱，脚本以进程权限运行。明确约定：**脚本由用户本人提供，视为可信输入**；产品不提供、不鼓励加载来历不明的脚本（写入 README 安全说明）。
- **依赖归属**（R-14）：QJSEngine 属 `Qt6::Qml` 模块而非 Core——技术栈与打包预算计入 `Qt6Qml.dll`（约数 MB），第 1 节技术栈表已同步。
- **QJSEngine 实例归属**：解析线程持有唯一的执行实例；GUI 脚本编辑器的语法检查使用 **GUI 线程内独立的临时 QJSEngine 实例**（只做 checkSyntax，不复用、不共享任何对象），两实例零交叉，杜绝跨线程访问。
- 编辑器提供错误行号定位与 2 个内置示例（固定 4 字节 float 帧 / ASCII CSV）。

### 4.4 协议热切换的跨线程同步（阻塞项 #5 落实）

- 切换入口只在 GUI 线程/CLI，切换动作只在解析线程执行：GUI 向**控制命令邮箱**（互斥锁保护的命令队列 + 条件变量）投递 `SwitchProtocol` 命令。
- 解析线程在每个批次边界检查邮箱：收到切换命令 → 停止取数 → 旧 parser `reset()` 并销毁 → **清空 SPSC 环形缓冲**（读写序号对齐，丢弃计数累加）→ 构造新 parser（Script 协议此时重建 QJSEngine 实例）→ 恢复取数。
- 通道数变更、脚本重载走同一命令通道，复用同一暂停-重建-恢复时序。

### 4.5 RawData

官方另有 RawData（不解析纯透传）。**v1.0 的"串口调试助手模式"等价实现为：协议选 RawData 时不建 ChannelStore、仅走接收区显示**（接收区显示模型见 8.2），随 F-01 自然覆盖。

## 5. GUI 设计与波形渲染

### 5.1 GUI 视觉规范（Glassmorphism，Grayson 2026-11-16 指定）

> 参照实现：Grayson 的 git-helper（PyQt6）——真玻璃拟态（抓桌面后模糊作为背景）、无边框、自绘标题栏。GayoSpace 沿用同一风格语言，C++/Qt6 Widgets 实现如下：

- **无边框 + 自绘标题栏**：`Qt::FramelessWindowHint`，自绘标题栏含拖拽、双击最大化、最小化/最大化/关闭，右键系统菜单保留；DWM 贴边分屏（Win+←/→）经 `platform/` 层适配（v1.0 可接受缺失，进移植清单）。
- **真玻璃背景**：窗口半透明（`WA_TranslucentBackground`），背景 = 抓取窗口后方桌面区域（`QScreen::grabWindow`）→ 高斯模糊 → 缓存为位图；触发重抓时机：窗口移动/缩放结束、系统壁纸或显示器变更，移动过程中复用旧背景（性能优先）。**多显示器 / DPI 缩放场景**：抓取目标屏为窗口所在屏（`screen()`），DPI 缩放按 devicePixelRatio 处理；HDR 场景按 SDR 抓取（色彩偏差可接受）；异常（抓取失败）回落纯色主题。
- **毛玻璃面板**：内容面板用 QSS `rgba` 背景 + 圆角 + 1px 半透明描边，叠在模糊背景上。
- **玻璃浓度可调**：沿用 git-helper 的"玻璃浓度"参数（模糊半径 + 面板透明度），写入会话配置。
- **性能模式降级**（PID 调参场景保命开关）：开启后停用实时重抓（用启动时缓存背景或纯色主题），波形页保持全速。开关与会话配置一起记忆。
- **全特效不达标的降级路径**（评审 D 项）：性能基准以**默认配置（全特效）**为判定基准；若不达标 → 依次执行 ①玻璃浓度默认值下调（模糊半径减半）②背景改为静态壁纸模糊缓存（启动抓一次）③默认开启性能模式——每级降级记录进测试报告，P99 < 50ms 判据本身不动。
- **Qwt × 透明背景兼容性 spike（编码启动前验证项 #1）**：Qwt canvas 默认不透明，需实测 `setCanvasBackground(Qt::transparent)` + `WA_TranslucentBackground` 在玻璃面板上的效果与性能；不满足则波形页局部降级为不透明底板（视觉让位性能，方向提前定死）。验证结论回填本节。
- UI 代码含合规字符串（AGENTS.md 第三节）。

### 5.2 波形渲染选型对比（结论：Qwt）

| 方案 | License | 实时性能 | 工作量 | 结论 |
| --- | --- | --- | --- | --- |
| QCustomPlot | **GPLv3 / 商业双许可** | 优 | 低 | **排除**：GPL 传染，与本项目 MIT 冲突 |
| Qt Charts | GPLv3（开源版） | 良 | 低 | 排除：同上 License 顾虑 |
| Qwt | **已核对**：Qwt License v1.0 = LGPL + 例外①子类化 Qwt 控件不构成衍生作品 ②静态链接不构成衍生作品（出处：https://qwt.sourceforge.io/qwtlicense.html ，核对日 2026-11-16） | 良（8ch@1kHz 实践充分） | 中 | **定稿采用**；动态链接，列入 THIRD-PARTY-NOTICES |
| QPainter/QOpenGLWidget 自绘 | 无依赖 | 优（上限最高） | 高 | 备选：Qwt 实测不达标时启动（DoD 2.3 性能基准不通过即触发） |

### 5.3 波形交互功能落点（PRD F-05 逐项）

| PRD 功能 | 实现落点 |
| --- | --- |
| 缩放/平移 | QwtPlot zoomer/panner，作用于时间窗与幅值轴 |
| 自适应 | 双击画布回到"跟随最新 + 自动幅值"模式 |
| 暂停/恢复 | **冻结显示、采集照常**（与接收区暂停语义一致）；恢复时跳到最新 |
| 通道开关/颜色 | 波形页侧栏开关 + 颜色选择，写入会话配置 |

## 6. CLI 输出契约（稳定契约，向后兼容承诺自此版起）

```
用法（v1.0 三类场景，对应 PRD F-07；list-ports 经 2026-11-16 Grayson 批准加入）：
  gayospace-cli capture --port COM3 --baud 115200 --protocol justfloat|firewater|script [--script file.js]
                    --channels N [--rate FS] (--duration SEC | --samples COUNT) --out data.csv|data.json [--hex-echo]
  gayospace-cli send --port COM3 --baud 115200 --data "01 02 03" [--hex] [--repeat N --interval MS]
  gayospace-cli list-ports    # 枚举可用串口（JSON 输出：设备名、描述、硬件 ID），供 Agent/脚本探测
  gayospace-cli --version
```

- **`--rate FS`**（R-06）：采样率 Hz，capture 可选；缺省时 JSON 输出 `t_index`（样本序号）替代 `t_ms`（时间基准语义见 3.4——宁可显式缺省，不静默失真）。

> 二进制定名 `gayospace-cli.exe`（2026-11-18 R-04）：与 GUI `GayoSpace.exe` 在 Windows 大小写不敏感文件系统上无法同目录共存，故 CLI 不得命名为 `gayospace.exe`。

- **`--out` 对 capture 为必选**（评审 #9）：省略 = 参数错误，退出码 2。理由：stdout 仅人类可读进度/日志，结构化数据只落文件，管道判读永不混入噪声。
- JSON 输出 schema（capture）：

```json
{
  "meta": {"tool": "gayospace", "version": "1.0.0", "port": "COM3", "baud": 115200,
           "protocol": "justfloat", "channels": 8, "rate": 1000, "started_at": "2026-11-16T12:00:00+08:00"},
  "samples": [{"t_ms": 0, "ch": [1.0, 2.0]}, {"t_ms": 1, "ch": [1.1, 2.0]}],
  "stats": {"rx_bytes": 320000, "dropped_bytes": 0, "rx_dropped": 0, "parse_errors": 0}
}
```

- `dropped_bytes` = 写侧满载跳过的未读字节（丢旧）；`rx_dropped` = 写侧在读侧拷贝期间（owner CAS 获取失败）丢弃的新到字节（R-34/R-59 定稿，语义对照见 docs/ts-pipeline-concurrency.md §2.2）；`parse_errors` = 解析错误帧数。三者独立计数。
- **`rx_bytes` 口径（R-86，2026-11-23 定）**：= `ring.writePos() + ring.rxDropped()`，即**串口收到的总字节**（接纳入 ring + 满载被拒）。CLI/GUI 不得各自解释——组合规则以本节为准，`PipelineStats.ringBytes` 只含 writePos 部分，组装点在 CLI。

- `t_ms` = round(frame_index × 1000 / rate)（R-06 时间基准语义，见 3.4）；`--rate` 缺省时该字段改名为 `t_index`（原始帧序号）。
- **JSON 通道值 NaN/±Inf 输出 `null`**（R-116，2026-11-24 Grayson 批准）：JSON 标准不允许 NaN/Inf 字面量，输出 `null` 与 CSV 空字段语义对齐；调用方解析时需区分 `null`（无效样本）与数值。

- CSV：首行 `t_ms,ch0,ch1,...`；`--rate` 缺省时首列改名 `t_index`（与 JSON meta 同规则，R-40 统一口径），NaN 输出空字段。
- **退出码边界**（评审 #9）：`0` 成功（**个别帧 parse_error 是统计项，计入 `stats.parse_errors`，不影响退出码 0**）；`2` 参数错误；`3` 串口错误（打不开 / 中途断开且重连失败 / **端口被 GUI 或其他程序占用** / **写失败或写超时（含 drain 超时，R-130，send 路径见 §6.1）**）；`4` 协议致命错误（**仅两类：脚本抛出未捕获异常、脚本执行超时被强制中断**）；`5` IO 错误（落盘失败）；`130` 用户中断（Ctrl+C，已收数据照常落盘后退出——**"落盘"仅 capture；send 无落盘**）。Agent 调用方判定：退出码 0 且 parse_errors 少量 → 可信数据；退出码 4 → 脚本问题，重试无效；退出码 3 → 环境/占用/写通路问题（capture 数据侧可换端口重试；send 写失败需区分端口与驱动状态）。
- **130 的实现约束**（R-13）：Windows 控制台 Ctrl+C 需 `SetConsoleCtrlHandler`——平台 API，实现收敛 `src/platform/` 并同步第 11 节符号表（已登记）；禁止在 core/cli 内联平台调用。
- **JSON 流式写**（R-13）：samples 不全量驻留内存（10 分钟 × 8ch × 1kHz ≈ 30~60 MB 文本）；实现为**流式分段写**——`meta` 行 → 逐帧 sample 行（JSON Lines 语义，外层包装保持单文件合法 JSON：逐段写 `"samples": [` + 行间逗号 + `], "stats": {...}` 收尾）。CSV 天然流式。`--duration` 上限 3600 秒（防呆，超出即参数错误退出码 2）。
- 完整性：异常退出时已解析数据必须落盘（**原子写语义修正，R-13**：临时文件写完 → `MoveFileEx(MOVEFILE_REPLACE_EXISTING)` 覆盖目标——原文"临时文件 + rename"在 Windows 上目标已存在时 `QFile::rename` 返回 false，重复执行 `--out data.csv` 必失败；`MoveFileEx` 调用收敛 `platform/`，符号表已登记）。
- **端口占用提示**：CLI 打不开端口时错误信息注明"可能被 GUI 或其他程序占用"。

### 6.1 send 契约（R-124，2026-11-25 Grayson 拍板七项；批次③）

1. **编码**：无 `--hex` 时 `--data` 按字面 UTF-8 原样发送（含空白字符），**不做**隐式 hex 识别——`"01 02 03"` 是 8 字符还是 3 字节属经典歧义，禁止靠猜（防静默错发）。
2. **`--hex` 词法**：接受 `[0-9A-Fa-f]`，空白/逗号作分隔，`0x` 前缀可带；**奇数位（每字节须 2 位）拒绝**——退出码 2，报"每字节须 2 位"（`1` 究竟补位成 `0x01` 还是截断有歧义）；空串/只剩分隔符 → 退出码 2。
3. **`--repeat` / `--interval` 边界**：`--repeat` ∈ [1, 100000]，越界退出码 2；`--interval` 缺省 0 且允许 0（连发不等待，**不沿用 GUI 的 100ms 下限**——CLI 无人工操作约束）；`--interval` 出现但 `--repeat` 缺省 = 只发 1 次，不报错。**实现约束（R-129）**：批次① 已合并解析器 `--repeat` 谓词仅有下限（`v >= 1`），批次③ 必须同步收紧为 `v >= 1 && v <= 100000` 并补上界向量（`--repeat 100001 → 2`、`--repeat 100000 → 放行`；解析层断言，不真跑发送）。
4. **写失败判定（R-139 修订，2026-11-25）**：**写路径上报写失败 → 退出码 3**（触发条件：底层 `writeBytes` 返回 < 0 / 持续返回 0 至总超时 5 s 仍失败）；部分写（< 待发长度）由写路径内部续写剩余，**不重发**已写部分——判定主体是 SendQueue/SerialChannel 写路径（§8.1），CLI 只消费其上报结果；`writeBytes` 返回值语义归 `ISerialTransport`（CLI 禁止直连，见本节写路径架构条）。
5. **退出前 drain（R-127 修订，2026-11-25）**：全部入队后**循环等待至 `pendingWriteBytes() == 0`**，截止时间 = `max(1000, 理论时长 × 2 + 500)` ms（理论时长 = 待发总字节 × 10 / 波特率 × 1000，8N1 每字节约 10 bit）；超时 → 清队列、退出码 3。**实现约束**：不得以单次 `waitForBytesWritten(1000)` 判定排空——Qt 语义是"一次 payload 写出即返回"，多帧数据下会提前返回 true（正是要防的"报成功但没发出去"）；固定 1000 ms 在低速波特率（如 1200 bps × 256 B ≈ 2.13 s）会把"正常但慢"误判为失败。`flush()` 在前，仅用于把已入队数据推给内核。
6. **Ctrl+C（130）**：中断后续 repeat 轮次，已发出的不撤回，退出码 130（与 capture 一致：用户中断 ≠ 成功）。
7. **stdout**：仅人类可读一行（如 `sent 9 bytes x5`，全 ASCII——Windows 936 代码页下非 ASCII 字符乱码，R-135），**不加** `--out`/JSON（send 无结构化数据落盘需求，加即扩契约）。
- **校验先于开端口（R-128）**：参数校验（含 `--hex` 词法，`SendCodec::decode()`）一律先于打开端口——任一参数错误即退出码 2，与端口是否可用无关（否则"坏参数 + 忙端口"组合下验收①永远测不出 2）。`SendCodec` 调用点钉死在 CLI 校验阶段（纯函数、可单测），不在写循环里。
- **写路径架构（R-125，2026-11-25 Grayson 拍板：下沉 core）**：send 写路径经 `SerialChannel::write()` 入队 + `pendingWriteBytes()` 查询，与 GUI §8.1 发送队列共用同一条写路径（队列机制见 §8.1）——CLI 只负责编码 + 排程 + 退出码映射，禁止 CLI 直连 `transport->writeBytes()`（两条写路径的失败语义必然漂移，R-86 同类教训）。
- **占用提示统一实现（R-132）**：TS §6"可能被 GUI 或其他程序占用"提示在已合并代码中缺失（存量违约）——批次③ 抽 `formatOpenError(port, error)` 纯函数（`cli/` 内、可单测），capture 与 send 共用同一实现，不各写一份（R-86/R-125 同一原则）；批次③ 落地时补齐 capture 存量路径。
- **DoD 验收挂钩**：见 DoD 2.2 CLI 行新增验收①~③（①`--hex` 非法输入退出 2 且不写口；②端口被占用退出 3 且 stderr 含"可能被占用"；③中途拔线重连失败退出 3 含"连接已断开"措辞）。

## 7. 会话配置

- **路径策略（便携模式，R-01 方案 A，2026-11-18 Grayson 拍板）**：v1.0 默认便携模式——会话配置存 **exe 同目录 `./config/`**，崩溃转储存 `./crash/`；启动时对 exe 目录做一次写探测（创建并删除临时文件），失败则回落 `%LOCALAPPDATA%/GayoSpace/` 并**状态栏提示"已切换到用户目录模式"**。注册表零写入、系统目录零写入（对应 DoD 4.3）。
- **回落路径的实现口径**：用 `QStandardPaths::AppConfigLocation`（Qt 6 实测语义：Windows 下为 `C:/Users/<USER>/AppData/Local/<APPNAME>`，**Local 而非 Roaming**——本节原文 `%APPDATA%` 为事实性错误，2026-11-18 修正）。
- **GUI 入口必须设置** `organizationName` / `applicationName` / `applicationVersion`（`main()` 开头），保证回落路径可预测；配一条单测打印实际路径断言前缀。
- 内容：串口参数、协议选择与通道数、玻璃浓度、性能模式开关、波形通道开关与颜色、窗口布局、发送历史（上限 20 条）、CLI 默认参数。
- 损坏容错：解析失败即备份坏文件（`.bad` 后缀）+ 回落默认值 + 状态栏提示（对应 DoD 2.2 会话配置用例）。

## 8. 收发路径设计（阻塞项 #1 落实：F-01/F-02 全量落点）

### 8.1 发送路径

- **发送队列**：`core/serial/` worker 线程内（与 QSerialPort 同线程亲和性）维护 FIFO 发送队列，`writeBytes()` 入队 + 唤醒；`bytesWritten` 信号驱动逐块写出，队列深度上限 64 条（超出拒绝并提示），写失败清队列并报错。**批次③起写路径下沉 `SerialChannel`（R-125，2026-11-25 Grayson 拍板）**：`SerialChannel::write()` 入队 + `pendingWriteBytes()` 为 CLI/GUI 共用唯一写入口（CLI 契约见 §6.1），禁止两条写路径并存。
- **Hex 输入校验**：输入框实时校验——仅允许十六进制字符与空白分隔；非法字符红显且**发送按钮禁用**（PRD 失效表"误发送"行）。Text 模式按所选编码（UTF-8 / GBK）转字节。
- **定时发送**：状态机 `Idle → Armed → Running → Stopped`；**Running 状态在标题栏和发送面板双处红点呼吸标识 + 可一键停止**；启动前弹出确认条显示目标端口与内容摘要（发送前二次校验）。周期范围 100ms～60s（**仅约束 GUI 定时发送**；CLI `--interval` 见 §6.1 第 3 条——缺省 0、允许 0，R-131）。
- **发送历史**：成功发送的内容自动入历史（去重），上限 20 条，持久化到会话配置；历史条目一键回填输入框。
- **快捷发送**：用户可把任意历史条目固定为快捷按钮（最多 8 个）。

### 8.2 接收区显示模型（评审 #10）

- **原始字节环形缓冲**（独立于数据引擎缓冲，4 MB 上限）：Text/Hex 双模式切换时**从原始字节重渲染**，不做增量转换，保证切换无损。
- **定时批量刷新**：GUI 线程 50ms 定时器批量 append（单次上限 4 KB 文本），杜绝逐字节 append 的性能瓶颈。
- **行数上限**：接收区最多保留 5000 行，超出丢最旧并提示计数；"冻结滚动"开关（用户上翻阅读时不自动跳底）。
- 时间戳：每批次首字节打点（可选开关），格式 `[HH:mm:ss.zzz]`。

### 8.3 串口重连策略（阻塞项 #6 落实）

- 触发：已打开状态下 read/write 错误或设备消失（热插拔事件）。
- 策略：自动重连开关开启时，**指数退避 1s → 2s → 4s → 5s（封顶）× 共 10 次**；任一次成功即清计数回到正常。
- 上限后行为：停止自动重连，状态栏常驻红显"已断开（自动重连失败 10 次）"+ "重连"按钮；不发通知弹窗（避免骚扰）。
- 限制：自动重连仅重试同参数同端口；端口被占用（错误码判定）不重试，直接进入上限后状态（避免无限刷，PRD 硬要求）。

## 9. 性能指标分配（对齐 PRD F-05 / DoD 2.3）

| 指标 | 目标 | 分配 |
| --- | --- | --- |
| 数据面吞吐 | 8ch × 1kHz（JustFloat ≈ **36 kB/s 含帧尾**） | 串口层 ≥ 1 Mbit/s 不丢字节；解析线程单核占用 < 20% |
| 渲染面 | P99 UI 事件循环延迟 < 50ms | 渲染定时器 30fps + seqlock 快照；绘制耗时单帧 < 15ms |
| 丢包 | 丢弃计数 = 0（DoD 组合判据） | 环形缓冲 4MB ≈ 111 秒缓冲深度 @1kHz |
| 启动 | GUI 冷启动 < 2s，**含玻璃背景首次抓屏与模糊（预算 ≤ 300ms）** | 延迟加载脚本引擎与波形页 |

校准：DoD 2.3 要求阈值经真实 PID 场景感知验证（Grayson 参与）后锁定。

## 10. 失效模式设计对策（阻塞项 #2 落实：PRD 第 4 节 7 行逐行落点）

| # | 失效行 | 设计落点 | 测试形态（预标注） |
| --- | --- | --- | --- |
| 1 | 串口意外断开 | 8.3 重连策略（指数退避 ×10 上限 + 占用不重试） | 自动化（虚拟串口拔出模拟）+ 手工（物理拔线） |
| 2 | 接收缓冲暴涨 | 3.2 环形缓冲丢最旧 + 丢弃计数 + 状态栏提示 | 自动化（灌数用例） |
| 3 | 异常/残缺帧 | 4.1 重同步、4.2 宽容解析、4.3 脚本超时 100ms | 自动化（标准畸形数据集） |
| 4 | 波形绘制过载 | 渲染侧降采样（**R-08 修订**）：绘制点数超出画布像素预算时按**每像素桶取 min/max 两点**——点数不变、尖峰不丢、包络视觉正确；**禁用步长抽稀**（会整帧跳过偶发尖峰，PID 调参要找的超调/振铃/毛刺恰是尖峰）。降采样只改变显示密度、不改变存储与落盘数据；超载时**状态栏提示"当前负载超出实时绘制能力，已降采样"**。验收断言：注入单点尖峰后缩放到全窗，该尖峰仍可见 | 自动化（超载灌数 + 断言 P99、提示出现、尖峰可见） |
| 5 | 误发送 | 8.1 Hex 实时校验 + 定时发送状态机（双处红点标识 + 启动确认条 + 一键停止） | 自动化（校验规则）+ 手工（UI 状态机） |
| 6 | 日志文件写入失败 | GUI 日志写失败：停写 + 内存缓冲保留（上限 10MB，超限丢最旧）+ 弹窗提示原因，恢复写路径后可续写；CLI 落盘失败 = 退出码 5 | 自动化（只读目录/配额盘模拟） |
| 7 | 软件崩溃 | `platform/` 层崩溃钩子（**R-27 覆盖面修订**）：`SetUnhandledExceptionFilter` → MiniDumpWriteDump 写转储（含版本号、异常码），**补 `AddVectoredExceptionHandler`（抓栈溢出/堆破坏的第一现场）+ `std::set_terminate`（catch abort/terminate 路径）**；明示 best-effort 边界：纯硬件级进程被杀、断电等场景抓不到，不承诺 100%。**转储路径（R-01 方案 A）**：便携模式写 exe 同目录 `./crash/`；回落用户目录模式时写 `%LOCALAPPDATA%/GayoSpace/crash/`。**下次启动检测到新转储即提示"上次异常退出，转储文件位置：…"**（仅提示，不自动上传）。Win32 调用收敛在 `platform/`，扫描规则豁免区；**实现时注意：`platform/` 库必须被 GUI 与 CLI 两个 target 显式链接（当前是空 INTERFACE 库、无 target 链接，接线遗漏会导致钩子不生效）** | 手工（注入空指针验证转储与提示） |

## 11. 平台层边界与 Win32 扫描规则

- **唯一豁免目录**：`src/platform/`（v1.0 已知内容：崩溃转储钩子、CLI Ctrl+C 处理、原子替换写；潜在：DWM 贴边适配）。
- **平台层符号表初始版**（新增/变更走维护机制，随实现同步扩充）：`SetUnhandledExceptionFilter`、`AddVectoredExceptionHandler`、`MiniDumpWriteDump`、`SetConsoleCtrlHandler`、`MoveFileEx`（原子替换语义）。
- **CI 扫描规则**（对 `src/core`、`src/gui`、`src/cli`、`src/app` 生效；命中即红）：
  - 头文件包含：`<windows.h>`、`<windowsx.h>`、`<winsock2.h>`、`<process.h>`、`<io.h>`、`<direct.h>`、`<conio.h>`
  - **API 调用形态匹配**（评审细节项：不做裸字符串匹配，避免误伤注释/日志文本）：`::CreateFile(`、`CreateFileW(`、`CreateFileA(`、`RegOpenKey`、`::MessageBox(`、`MessageBoxW(`、`CoInitialize`、`WaitForSingleObject`、`::GetLastError(`；对 `//` 与 `/* */` 注释内容及字符串字面量先行剔除后再匹配（词法级处理，CI 侧 Python 预处理脚本 + 规则表实现）。
- **维护机制**（DoD v1.1 2.3）：新增平台专属调用 → 先在 `platform/` 立项并同步更新符号表与扫描规则 → 变更走 AGENTS.md 第十二节流程，防止符号表腐化。

## 12. 测试策略（落实 DoD 第 2 节）

- **覆盖率统计目录（锁定覆盖率分母）与阈值论证**：`core/protocol/`、`core/pipeline/` ≥ 80%——纯算法、无外部依赖，可全自动化；`core/serial/`、`core/session/` ≥ 60%——论证：串口层错误路径依赖真机/虚拟串口环境，会话配置依赖文件系统边界场景，两类中约四成用例只能手工执行，故阈值放宽并强制手工用例可重复（DoD 2.4）；`gui/`、`cli/` 不计入行覆盖率，以集成与手工用例覆盖。
- 必测场景矩阵、失效模式逐行标注（第 10 节表已给出预标注）、flaky 规则按 DoD 执行，测试报告模板随框架工程一起搭建。
- 虚拟串口对捞：com0com（开源虚拟串口对，Win10/11 签名安装方式**待核 #3**）+ 一端喂标准数据集文件。

## 13. TODO 池架构预留（对齐 PRD 2.2，逐条落位）

| TODO | 本 TS 预留 |
| --- | --- |
| 001 自动化脚本 | QJSEngine 引擎插槽已隔离在 `protocol/script`，v2.0 扩 API 面不动架构 |
| 002 网络通道 | `core/serial/` 之上抽象 `IDataChannel` 接口（读写 + 状态信号），TCP/UDP 实现时新增 `core/net/` |
| 003 控制面板 | ChannelStore 双向预留"下行命令"消息接口（复用控制命令邮箱） |
| 004 多会话标签 | 前端会话对象化（Session 类），GUI 标签页 = 多实例，core 无需改动 |
| 007 跨平台 | `platform/` 目录 + 扫描机制（第 11 节），CMake 顶层按平台条件编译 |
| 009 安装包 | v1.0 仅便携 zip；打包脚本目录 `packaging/` 预留 |

## 14. 待核项清单（编码启动前硬前置 = 无；#3/#5 已转入编码阶段 spike）

| # | 项 | 说明 | 状态 |
| --- | --- | --- | --- |
| 1 | Qt 版本 | — | **已关闭**：Qt 6.8 LTS（MSVC 2022 x64）；6.12 LTS 为最后支持 Win10 的版本，列入 v2.0 + TODO-007 升级候选 |
| 2 | OpenCppCoverage | — | **已关闭**（选项 A）：0.9.9.0 CLI，上游已归档，v1.0 不迁移；v1.1 评估 Microsoft.CodeCoverage.Console（需确认 CI VS 许可层级） |
| 3 | com0com 签名安装方式 | Win10/11 验证 | **编码阶段 spike**（搭集成测试环境时执行） |
| 4 | FireWater 协议比对 | 规则已定（见 4.2，不再变更） | **收窄为边界案例 spike**：仅实测确认边界案例 |
| 5 | Qwt 透明背景 | 见 5.1，结论回填 | **编码阶段 spike #1**（Qwt × Glassmorphism 兼容性实测） |
| 6 | Qt 6.8 LTS 开源用户可得的补丁范围与升级节奏预案（R-21，2026-11-18 新增） | LTS 补丁面向商业客户（出处见第 1 节）；需核实开源用户能拿到的 6.8.x 补丁窗口，并给出安全补丁到期后的升级路径预案（如届时评估 6.12 LTS 或社区维护分支） | 编码阶段并行核实，结论回填第 1 节 |

## 15. 决议记录

- 2026-11-16：四项决议（帧尾更正 / 仅便携 zip / list-ports / Qwt 采用）经 Grayson 批准落地，详见 CHANGELOG。
- 2026-11-16：Glassmorphism 视觉规范经 Grayson 指定纳入 5.1 节。
- 2026-11-16：外部评审（DeepSeek）6 阻塞项 + 重要/细节项全部纳入本 v0.2，逐条落点见第 8、10 节及正文标注。
- 2026-11-16：待核项三项关闭（Grayson 提交结论）——Qt 6.8 LTS 锁定、OpenCppCoverage 选项 A（v1.0 沿用 + v1.1 评估迁移）、FireWater any: 规则核对一致。
- 2026-11-18：第 1 轮全面评审（Grayson × DeepSeek，本地评审记录，未随仓库发布）R-01/R-04/R-06~R-09/R-13/R-14/R-21/R-22/R-25/R-26/R-27 纳入本版：便携模式方案 A（Grayson 拍板）、波形数据模型四项修订、协议解析规则收窄、CLI 契约落地前提补齐、新增待核 #6（Qt 补丁范围）。
