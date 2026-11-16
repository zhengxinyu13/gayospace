# GayoSpace 软件技术方案（TS-SW）v1.0（定稿）

> 状态：**定稿（共识版 v1.0）**——2026-11-16 Grayson 确认；后续变更走 AGENTS.md 第十二节变更管理
> 日期：2026-11-16
> 上游依据：docs/PRD.md（基础共识版 v1.0）、docs/DoD.md（定稿 v1.1）
> 本项目无硬件，无 TS-HW / HSI；本 TS 是唯一的"怎么做"事实来源。
> v0.2 变更：依据外部评审（DeepSeek，2026-11-16，Grayson 提交）补 6 处阻塞缺口（发送路径、失效模式 4 行、ChannelStore 数据模型、IProtocolParser 接口、协议热切换跨线程同步、串口重连策略）与全部重要/细节项；章节重排。
> v0.3 变更：待核项 #1/#2/#4 关闭——Qt 锁定 6.8 LTS（Grayson 2026-11-16 决策）；OpenCppCoverage 维持 v1.0 使用 + v1.1 评估迁移（选项 A，Grayson 确认）；FireWater any: 判定规则经核对与官方行为一致并回填出处。
> v1.0 定稿变更：按 Grayson 最终回填修订技术栈两行与待核清单口径；三项待核关闭，两项（com0com、Qwt spike）转入编码阶段执行。

## 1. 技术栈总表

| 项 | 选型 | 说明 |
| --- | --- | --- |
| 语言 | C++20 | `std::span`、`std::atomic` 等现代设施；与参考项目 serial_debug_tool 同代 |
| UI 框架 | Qt 6.8 LTS（MSVC 2022 x64 工具链） | Widgets 优先于 QML；6.8 为 LTS，5 年维护窗口覆盖 v1.0 生命周期；6.12 LTS 为最后一个支持 Win10 的版本，列入 v2.0 + TODO-007 升级候选，不进 v1.0 基线 |
| 构建 | CMake ≥ 3.21 | 前置依赖脚本化；MSVC x64 工具链 |
| 脚本引擎 | QJSEngine | Qt 自带零第三方依赖，仅用于协议解析（PRD F-04 边界） |
| 波形渲染 | Qwt | 已核对：Qwt License v1.0 = LGPL + 子类化/静态链接例外（出处 https://qwt.sourceforge.io/qwtlicense.html ，核对日 2026-11-16）；动态链接，列入 THIRD-PARTY-NOTICES |
| 单元测试 | Catch2 v3 | BSL-1.0 宽松许可；Qt 信号槽相关测试辅以 Qt Test（DoD 2.1 硬前置，随本 TS 定稿锁定） |
| 覆盖率 | OpenCppCoverage 0.9.9.0（CLI） | 生成 Cobertura 报告入 CI；上游仓库 2026-09 已归档（只读），CLI 在 VS 2022 构建产物上可正常工作，v1.0 不迁移；v1.1 评估迁移至 Microsoft.CodeCoverage.Console（需确认 CI 的 VS 许可层级），归档风险登记于第 14 节待核。**构建/测试期工具，License 与产品无关、不随产品分发**；THIRD-PARTY-NOTICES 分"运行时依赖"与"构建期工具"两节登记 |
| CI | GitHub Actions（windows-latest） | 构建 + 单测 + Win32 扫描 + UI 字符串检查 + 覆盖率 |
| 版本 | SemVer，编译期注入 `GAYOSPACE_VERSION` | 运行时"关于"对话框与 CLI `--version` 可读出 |

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
└── app/                   # main 入口：--cli 标志分流到 cli/ 或 gui/
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

### 3.2 SPSC 环形缓冲

- 存储：预分配连续 `std::byte` 数组（默认 4 MB，评审可调），读写位置用 `std::atomic<uint64_t>` 累计序号（单调递增，取模寻址），无锁。
- 传递单位：字节流（不按块分配内存，解析线程按序号区间直接读，**零拷贝**——读侧在写侧未越过头部的区间内可安全读）。
- 满载策略：丢最旧（推进读序号）并累加丢弃字节计数（PRD 失效表"接收缓冲暴涨"行）。
- 深度：8ch×1kHz JustFloat 帧长 36 B（8×4+4），36 kB/s 下 4 MB ≈ **111 秒**缓冲深度，远超渲染周期。

### 3.3 ChannelStore（核心数据模型，阻塞项 #3 落实）

- **通道数上限**：32 通道（v1.0 上限，编译期常量 `MAX_CHANNELS`；PRD F-05 要求 ≥8）。
- **采样存储**：每通道一个环形 double 数组，容量按时间窗 600 秒 @1kHz（=600k 点/通道，8 通道 ≈ 38 MB 上界，可配）；时间戳由解析线程统一打（steady_clock）。
- **并发模型**：**双缓冲 + seqlock**——解析线程写后台缓冲，写完原子递增序号（奇=写中）；GUI/CLI 读时取序号，读完复核，奇数或序号变化则重读（有界重试 2 次，仍未取到即跳过本渲染周期，下周期再取）。单写多读，无需互斥锁，读侧无阻塞。
- **快照语义**：渲染每次取"最新完整状态"，按需回溯窗口（默认显示最近 10 秒，缩放可到全缓冲）；暂停模式下照常写入、仅渲染不推进（见 5.3）。
- **协议差异的通道数语义**：JustFloat = 固定 N（用户配置）；FireWater = 按行字段数动态（以首行为基准，后续行字段数变化计错误）；Script = 按 `parse` 返回数组长度动态（上限 32，超限截断+计数）。通道数变化时 ChannelStore 重建列布局、波形页提示通道映射已重排。

## 4. 协议引擎

### 4.0 IProtocolParser 接口（阻塞项 #4 落实）

```cpp
struct ParseFrame {
    std::chrono::steady_clock::time_point t;
    std::array<float, MAX_CHANNELS> ch;   // 仅前 ch_count 有效
    uint8_t ch_count;
};

struct ParseStats { uint64_t frames_ok, frames_bad, bytes_dropped_sync; };

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
- **JustFloat 的 CH_COUNT 来源**：字节流不含通道数，**由用户配置**（GUI 波形页配置项 / CLI `--channels N`，默认 4）。通道数变更路径：GUI 下发控制命令 → 解析线程暂停 → `reset()` + 清空环形缓冲 → 以新 N 重建 parser → 恢复（见 4.4）。配置错误（N 与实际不符）表现为持续 `frames_bad` 增长，状态栏提示"通道数与数据不匹配，请检查通道数设置"。

### 4.1 JustFloat（出处：https://www.vofa.plus/docs/learning/dataengines/justfloat/ ，核对日 2026-11-16）

```c
struct Frame {
    float ch_data[CH_COUNT];                        // 小端 IEEE754 float 数组
    unsigned char tail[4]{0x00, 0x00, 0x80, 0x7f};  // 帧尾（小端 +Inf），无帧头
};
```

- 解析策略：扫描字节流找帧尾 → 回推 `CH_COUNT×4` 字节为有效负载 → 重同步；连续失败 8 帧（可配）后半字节步进重同步。
- 帧尾歧义处理：负载中通道值恰为 +Inf 时字节序列与帧尾重合——策略为**帧尾命中后校验回推区间对齐率**（帧距 = N×4+4 整数倍），歧义帧按最老匹配优先丢弃并计数；真 +Inf 通道值本身在 PID 场景中即异常值，丢帧可接受且可从统计发现。

### 4.2 FireWater（出处：https://www.vofa.plus/docs/learning/dataengines/firewater/ ，核对日 2026-11-16）

- 格式：`"<any>:ch0,ch1,...,chN\n"`；`any` 与冒号可省略；`\n` 不可省略，兼容 `\r\n` 与 `\n\r`；`any` 不得为 `image`。
- **`any:` 前缀判定规则（已核对，2026-11-16）**：行首至第一个 `:` 之间的片段，若不含数字则视为前缀剔除（如 `channels:1,2,3`）；若含数字则不剔除，整行进入宽容解析（如 `1.0:2.0,3.0` → 字段 `1.0:2.0` 经 `strtof` 解析出 1.0，冒号终止该字段）。
- 判定规则已与 VOFA+ 官方文档核对（出处 https://vofa.plus/docs/learning/dataengines/firewater/ ，核对日 2026-11-16）。官方约定 any 与冒号可为空，本条"行首冒号前片段是否含数字"的判定是官方语义的工程落地；待核 #4 的 spike 用于确认边界案例，规则本身不再变更。
- 解析策略：按行缓冲 → 前缀判定 → 逗号切分 → 逐字段 `strtof` 宽容解析（失败字段置 NaN 并计入错误计数，不丢整行）。

### 4.3 Script（自定义 JS 协议）

脚本契约（QJSEngine 全局函数，用户实现）：

```js
// 输入：bytes 为本次到达的原始字节（Uint8Array，跨调用保持状态由用户负责）
// 输出：本次解析出的通道数值数组；无完整帧返回空数组
function parse(bytes) { ... }
// 可选：清状态（协议切换/串口重开时由引擎调用）
function reset() { ... }
```

- 引擎侧保证：喂入顺序与串口到达顺序一致；错误即捕获、不崩溃；单次执行超时 100ms 强制中断（可配）。
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
  gayospace capture --port COM3 --baud 115200 --protocol justfloat|firewater|script [--script file.js]
                    --channels N (--duration SEC | --samples COUNT) --out data.csv|data.json [--hex-echo]
  gayospace send --port COM3 --baud 115200 --data "01 02 03" [--hex] [--repeat N --interval MS]
  gayospace list-ports        # 枚举可用串口（JSON 输出：设备名、描述、硬件 ID），供 Agent/脚本探测
  gayospace --version
```

- **`--out` 对 capture 为必选**（评审 #9）：省略 = 参数错误，退出码 2。理由：stdout 仅人类可读进度/日志，结构化数据只落文件，管道判读永不混入噪声。
- JSON 输出 schema（capture）：

```json
{
  "meta": {"tool": "gayospace", "version": "1.0.0", "port": "COM3", "baud": 115200,
           "protocol": "justfloat", "channels": 8, "started_at": "2026-11-16T12:00:00+08:00"},
  "samples": [{"t_ms": 0, "ch": [1.0, 2.0]}, {"t_ms": 1, "ch": [1.1, 2.0]}],
  "stats": {"rx_bytes": 320000, "dropped_bytes": 0, "parse_errors": 0}
}
```

- CSV：首行 `t_ms,ch0,ch1,...`，NaN 输出空字段。
- **退出码边界**（评审 #9）：`0` 成功（**个别帧 parse_error 是统计项，计入 `stats.parse_errors`，不影响退出码 0**）；`2` 参数错误；`3` 串口错误（打不开 / 中途断开且重连失败 / **端口被 GUI 或其他程序占用**）；`4` 协议致命错误（**仅两类：脚本抛出未捕获异常、脚本执行超时被强制中断**）；`5` IO 错误（落盘失败）；`130` 用户中断（Ctrl+C，已收数据照常落盘后退出）。Agent 调用方判定：退出码 0 且 parse_errors 少量 → 可信数据；退出码 4 → 脚本问题，重试无效；退出码 3 → 环境/占用问题，可换端口重试。
- 完整性：异常退出时已解析数据必须落盘（原子写：临时文件 + rename）。
- **端口占用提示**：CLI 打不开端口时错误信息注明"可能被 GUI 或其他程序占用"。

## 7. 会话配置

- 位置：`QStandardPaths::AppConfigLocation`（Windows 下 `%APPDATA%\GayoSpace\`）JSON 文件。
- 内容：串口参数、协议选择与通道数、玻璃浓度、性能模式开关、波形通道开关与颜色、窗口布局、发送历史（上限 20 条）、CLI 默认参数。
- 损坏容错：解析失败即备份坏文件（`.bad` 后缀）+ 回落默认值 + 状态栏提示（对应 DoD 2.2 会话配置用例）。

## 8. 收发路径设计（阻塞项 #1 落实：F-01/F-02 全量落点）

### 8.1 发送路径

- **发送队列**：`core/serial/` worker 线程内（与 QSerialPort 同线程亲和性）维护 FIFO 发送队列，`writeBytes()` 入队 + 唤醒；`bytesWritten` 信号驱动逐块写出，队列深度上限 64 条（超出拒绝并提示），写失败清队列并报错。
- **Hex 输入校验**：输入框实时校验——仅允许十六进制字符与空白分隔；非法字符红显且**发送按钮禁用**（PRD 失效表"误发送"行）。Text 模式按所选编码（UTF-8 / GBK）转字节。
- **定时发送**：状态机 `Idle → Armed → Running → Stopped`；**Running 状态在标题栏和发送面板双处红点呼吸标识 + 可一键停止**；启动前弹出确认条显示目标端口与内容摘要（发送前二次校验）。周期范围 100ms～60s。
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
| 4 | 波形绘制过载 | 渲染侧抽稀：全部通道合计单帧绘制点数上限 20k，超出按步长抽稀并**状态栏提示"当前负载超出实时绘制能力，已降采样"**；数据采集不丢（降的是显示密度） | 自动化（超载灌数 + 断言 P99 与提示出现） |
| 5 | 误发送 | 8.1 Hex 实时校验 + 定时发送状态机（双处红点标识 + 启动确认条 + 一键停止） | 自动化（校验规则）+ 手工（UI 状态机） |
| 6 | 日志文件写入失败 | GUI 日志写失败：停写 + 内存缓冲保留（上限 10MB，超限丢最旧）+ 弹窗提示原因，恢复写路径后可续写；CLI 落盘失败 = 退出码 5 | 自动化（只读目录/配额盘模拟） |
| 7 | 软件崩溃 | `platform/` 层注册 `SetUnhandledExceptionFilter` → MiniDumpWriteDump 写 `%APPDATA%\GayoSpace\crash\` 转储（含版本号、异常码）；**下次启动检测到新转储即提示"上次异常退出，转储文件位置：…"**（仅提示，不自动上传）。Win32 调用收敛在 `platform/`，扫描规则豁免区 | 手工（注入空指针验证转储与提示） |

## 11. 平台层边界与 Win32 扫描规则

- **唯一豁免目录**：`src/platform/`（v1.0 已知内容：崩溃转储钩子；潜在：DWM 贴边适配）。
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

## 15. 决议记录

- 2026-11-16：四项决议（帧尾更正 / 仅便携 zip / list-ports / Qwt 采用）经 Grayson 批准落地，详见 CHANGELOG。
- 2026-11-16：Glassmorphism 视觉规范经 Grayson 指定纳入 5.1 节。
- 2026-11-16：外部评审（DeepSeek）6 阻塞项 + 重要/细节项全部纳入本 v0.2，逐条落点见第 8、10 节及正文标注。
- 2026-11-16：待核项三项关闭（Grayson 提交结论）——Qt 6.8 LTS 锁定、OpenCppCoverage 选项 A（v1.0 沿用 + v1.1 评估迁移）、FireWater any: 规则核对一致。
