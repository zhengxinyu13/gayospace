# GayoSpace 软件技术方案（TS-SW）v0.1 草稿

> 状态：草稿，待 Grayson review（AGENTS.md 第一节第 6 步：TS 定稿后方可编码）
> 日期：2026-11-16
> 上游依据：docs/PRD.md（基础共识版 v1.0）、docs/DoD.md（定稿 v1.0）
> 本项目无硬件，无 TS-HW / HSI；本 TS 是唯一的"怎么做"事实来源。

## 1. 技术栈总表

| 项 | 选型 | 说明 |
| --- | --- | --- |
| 语言 | C++20 | `std::span`、`std::atomic` 等现代设施；与参考项目 serial_debug_tool 同代 |
| UI 框架 | Qt 6（Widgets） | Widgets 优先于 QML：桌面工具、重绘图、不追移动端动效；具体 Qt 小版本编码前核对最新 LTS（**待核**） |
| 构建 | CMake ≥ 3.21 | 前置依赖脚本化；MSVC x64 工具链 |
| 脚本引擎 | QJSEngine | Qt 自带零第三方依赖，仅用于协议解析（PRD F-04 边界） |
| 波形渲染 | Qwt（已核对：Qwt License v1.0 = LGPL + 子类化/静态链接例外，出处 https://qwt.sourceforge.io/qwtlicense.html ，核对日 2026-11-16） | MIT 项目合规，动态链接使用并列入 THIRD-PARTY-NOTICES |
| 单元测试 | Catch2 v3 | BSL-1.0 宽松许可；Qt 信号槽相关测试辅以 Qt Test（DoD 2.1 硬前置，随本 TS 定稿锁定） |
| 覆盖率 | OpenCppCoverage（MSVC） | 生成 Cobertura 报告入 CI（工具最新版本支持情况**待核**） |
| CI | GitHub Actions（windows-latest） | 构建 + 单测 + Win32 扫描 + UI 字符串检查 + 覆盖率 |
| 版本 | SemVer，编译期注入 `GAYOSPACE_VERSION` | 运行时"关于"对话框与 CLI `--version` 可读出（对应 AGENTS.md 9.1 固件宏要求的软件等价物，无 SKU/HW Rev 两项） |

## 2. 架构分层与目录结构

```
src/
├── core/                  # 无 UI、无平台 API 层（CI 扫描禁区外唯一豁免区见 platform/）
│   ├── serial/            # 串口封装：QSerialPort 包装、参数、热插拔、重连策略
│   ├── protocol/          # 数据引擎：justfloat / firewater / script 三实现 + IProtocolParser 接口
│   ├── pipeline/          # 环形缓冲、通道数据仓库（ChannelStore）、丢弃计数、降采样
│   ├── session/           # 会话配置读写（QSettings/JSON）
│   └── applogic/          # 收发编排、统计、日志（core 之上、前端之下的共享业务层）
├── cli/                   # headless 前端：参数解析、输出格式化、退出码
├── gui/                   # Widgets 前端：主窗口、波形页、串口面板、脚本编辑器
├── platform/              # 平台专属收敛层（唯一允许平台 API 的目录，见第 9 节）
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

```
[串口读线程] --QByteArray块--> [SPSC环形缓冲] --解析线程--> [IProtocolParser] --> [ChannelStore(原子快照)] --> [GUI 渲染定时器 30fps 抽样绘制]
                                     |                                                        |
                                     +--> [CLI 落盘]（收满 duration 或 Ctrl+C 时输出）          +--> [丢弃计数器 → 状态栏]
```

- **串口读线程**：QSerialPort readyRead 移入专职 worker 线程（moveToThread），整块搬运，不做解析。
- **环形缓冲**：无锁 SPSC（单生产者单消费者），容量默认 4 MB（TS 评审可调）；满载丢最旧并累加丢弃计数（PRD 失效表"接收缓冲暴涨"行的软件动作）。
- **解析线程**：从缓冲批量取数 → 按 IProtocolParser 喂入 → 解析结果（时间戳 + 通道数值数组）写入 ChannelStore。三个协议实现可热切换，切换时清空缓冲重同步。
- **渲染**：GUI 用 30ms 定时器取 ChannelStore 快照绘制，绘制频率与数据频率解耦——1kHz 灌数时不放大渲染压力（DoD 2.3 P99 < 50ms 的结构性保障）。
- **脚本执行隔离**：QJSEngine 实例绑定解析线程；脚本执行前置看门狗定时器，超时强制中断并上报错误（PRD 失效表"异常/残缺帧"行）。

## 4. 协议引擎（官方原文已核对）

### 4.1 JustFloat（出处：https://www.vofa.plus/docs/learning/dataengines/justfloat/ ，核对日 2026-11-16）

```c
struct Frame {
    float ch_data[CH_COUNT];              // 小端 IEEE754 float 数组，CH_COUNT 为通道数
    unsigned char tail[4]{0x00, 0x00, 0x80, 0x7f};  // 帧尾
};
```

- 帧尾四字节 `00 00 80 7F`（即小端表示的 +Inf），**无帧头**，靠帧尾切帧。
- 解析策略：扫描字节流找帧尾 → 回推 `CH_COUNT×4` 字节为有效负载 → 重同步；连续解析失败 N 帧（N=8，可配）后进入半字节步进重同步。
- ⚠️ **PRD F-03 草稿期占位写帧尾 `AA 01 00 00` 与官方不符，随本 TS review 一并申请更正**（变更申请，待 Grayson 批准后改 PRD）。

### 4.2 FireWater（出处：https://www.vofa.plus/docs/learning/dataengines/firewater/ ，核对日 2026-11-16）

- 格式：`"<any>:ch0,ch1,...,chN\n"`；`any` 与冒号可省略；`\n` 不可省略，兼容 `\r\n` 与 `\n\r`；`any` 不得为 `image`（官方保留前缀，图片解析不在本项目范围）。
- 解析策略：按行缓冲 → 去掉可选 `any:` 前缀 → 逗号切分 → 逐字段 `strtof` 宽容解析（失败字段置 NaN 并计入错误计数，不丢整行）。

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
- 编辑器提供错误行号定位与 2 个内置示例（固定 4 字节 float 帧 / ASCII CSV）。

### 4.4 RawData

官方另有 RawData（不解析纯透传）。**v1.0 的"串口调试助手模式"等价实现为：协议选 RawData 时不建 ChannelStore、仅走接收区显示**，成本极低，随 F-01 自然覆盖。

## 5.1 GUI 视觉规范（Glassmorphism，Grayson 2026-11-16 指定）

> 参照实现：Grayson 的 git-helper（PyQt6）——真玻璃拟态（抓桌面后模糊作为背景）、无边框、自绘标题栏。GayoSpace 沿用同一风格语言，C++/Qt6 Widgets 实现如下：

- **无边框 + 自绘标题栏**：`Qt::FramelessWindowHint`，自绘标题栏含拖拽、双击最大化、最小化/最大化/关闭，右键系统菜单保留；DWM 贴边分屏（Win+←/→）经 `platform/` 层适配（v1.0 可接受缺失，进移植清单）。
- **真玻璃背景**：窗口半透明（`WA_TranslucentBackground`），背景 = 抓取窗口后方桌面区域（`QScreen::grabWindow`）→ 高斯模糊 → 缓存为位图；触发重抓时机：窗口移动/缩放结束、系统壁纸或显示器变更，移动过程中复用旧背景（性能优先）。
- **毛玻璃面板**：内容面板用 QSS `rgba` 背景 + 圆角 + 1px 半透明描边，叠在模糊背景上。
- **玻璃浓度可调**：沿用 git-helper 的"玻璃浓度"参数（模糊半径 + 面板透明度），写入会话配置。
- **性能模式降级**（PID 调参场景保命开关）：开启后停用实时重抓（用启动时缓存背景或纯色主题），波形页保持全速。该开关与会话配置一起记忆；性能基准测试（DoD 2.3）在**性能模式关闭**（全特效）状态下跑，达标即代表特效不拖累波形。
- UI 代码含合规字符串（AGENTS.md 第三节）。

## 5.2 波形渲染选型对比（结论先行：Qwt）

| 方案 | License | 实时性能 | 工作量 | 结论 |
| --- | --- | --- | --- | --- |
| QCustomPlot | **GPLv3 / 商业双许可** | 优 | 低 | **排除**：GPL 传染，与本项目 MIT 冲突 |
| Qt Charts | GPLv3（开源版） | 良 | 低 | 排除：同上 License 顾虑（**待核**最新许可条款） |
| Qwt | **已核对**：Qwt License v1.0 = LGPL + 例外①子类化 Qwt 控件不构成衍生作品 ②静态链接不构成衍生作品（出处：https://qwt.sourceforge.io/qwtlicense.html ，核对日 2026-11-16） | 良（8ch@1kHz 实践充分） | 中 | **定稿采用**；动态链接，列入 THIRD-PARTY-NOTICES |
| QPainter/QOpenGLWidget 自绘 | 无依赖 | 优（上限最高） | 高 | 备选：Qwt 实测不达标时启动（DoD 2.3 性能基准不通过即触发） |

## 6. CLI 输出契约（稳定契约，向后兼容承诺自此版起）

```
用法（v1.0 三类场景，对应 PRD F-07；list-ports 经 2026-11-16 Grayson 批准加入）：
  gayospace capture --port COM3 --baud 115200 [--protocol justfloat|firewater|script --script file.js]
                    [--channels N] [--duration SEC | --samples COUNT] [--out data.csv|data.json] [--hex-echo]
  gayospace send --port COM3 --baud 115200 --data "01 02 03" [--hex] [--repeat N --interval MS]
  gayospace list-ports        # 枚举可用串口（JSON 输出：设备名、描述、硬件 ID），供 Agent/脚本探测
  gayospace --version
```

- JSON 输出 schema（capture，`--out data.json`）：

```json
{
  "meta": {"tool": "gayospace", "version": "1.0.0", "port": "COM3", "baud": 115200,
           "protocol": "justfloat", "channels": 8, "started_at": "2026-11-16T12:00:00+08:00"},
  "samples": [{"t_ms": 0, "ch": [1.0, 2.0, ...]}, ...],
  "stats": {"rx_bytes": 320000, "dropped_bytes": 0, "parse_errors": 0}
}
```

- CSV：首行 `t_ms,ch0,ch1,...`，NaN 输出空字段。
- stdout 仅输出人类可读进度/日志，结构化数据只落 `--out` 文件（管道友好：Agent 判读永不混入噪声）。
- **退出码契约**：`0` 成功；`2` 参数错误；`3` 串口错误（打不开/中途断开且重连失败）；`4` 协议/解析错误（脚本异常、超时）；`5` IO 错误（落盘失败）；`130` 用户中断（Ctrl+C，此时已收数据照常落盘后退出）。
- 完整性：异常退出时已解析数据必须落盘（原子写：临时文件 + rename）。

## 7. 会话配置

- 位置：`QStandardPaths::AppConfigLocation`（Windows 下 `%APPDATA%\GayoSpace\`）JSON 文件。
- 内容：串口参数、协议选择、波形通道开关与颜色、窗口布局、CLI 默认参数。
- 损坏容错：解析失败即备份坏文件（`.bad` 后缀）+ 回落默认值 + 状态栏提示（对应 DoD 2.2 会话配置用例）。

## 8. 性能指标分配（对齐 PRD F-05 / DoD 2.3）

| 指标 | 目标 | 分配 |
| --- | --- | --- |
| 数据面吞吐 | 8ch × 1kHz（JustFloat ≈ 32 kB/s） | 串口层 ≥ 1 Mbit/s 不丢字节；解析线程单核占用 < 20% |
| 渲染面 | P99 UI 事件循环延迟 < 50ms | 渲染定时器 30fps + 快照抽样；绘制耗时单帧 < 15ms |
| 丢包 | 丢弃计数 = 0（DoD 组合判据） | 环形缓冲 4MB ≈ 125 秒缓冲深度 @1kHz，远超渲染周期 |
| 启动 | GUI 冷启动 < 2s | 延迟加载脚本引擎与波形页 |

校准：DoD 2.3 要求阈值经真实 PID 场景感知验证（Grayson 参与）后锁定。

## 9. 平台层边界与 Win32 扫描规则初始版

- **唯一豁免目录**：`src/platform/`。v1.0 预计该目录近乎为空（Qt 已覆盖串口/路径/编码）——它是"为 macOS/Linux 移植时不得不写的差异"预留的收纳处（TODO-007）。
- **CI 扫描初始符号表**（对 `src/core`、`src/gui`、`src/cli`、`src/app` 生效；命中即红）：
  - 头文件包含：`<windows.h>`、`<windowsx.h>`、`<winsock2.h>`、`<process.h>`、`<io.h>`、`<direct.h>`、`<conio.h>`
  - API 符号：`RegOpenKey`、`CreateFile`（Win32 语义）、`SetNamedPipeHandleState`、`MessageBox`（非 Qt 前缀）、`CoInitialize`、`WaitForSingleObject`、`GetLastError`（非 Qt 错误路径）
- **维护机制**（DoD v1.0 2.3）：新增平台专属调用 → 先在 `platform/` 立项并同步更新符号表与扫描规则 → 变更走 AGENTS.md 第十二节流程，防止符号表腐化。

## 10. 测试策略（落实 DoD 第 2 节）

- **覆盖率统计目录（锁定覆盖率分母）**：`core/protocol/`、`core/pipeline/` 行覆盖率 ≥ 80%（DoD 基准线）；`core/serial/`、`core/session/` 计入统计、目标 ≥ 60%（依赖真机交互，部分用例手工）；`gui/`、`cli/` 不计入行覆盖率，以集成与手工用例覆盖。
- 必测场景矩阵、失效模式逐行标注、flaky 规则按 DoD 执行，测试报告模板随框架工程一起搭建。
- 虚拟串口对捞：com0com（开源虚拟串口对，**待核** Windows 10/11 签名安装方式）+ 一端喂标准数据集文件。

## 11. TODO 池架构预留（对齐 PRD 2.2，逐条落位）

| TODO | 本 TS 预留 |
| --- | --- |
| 001 自动化脚本 | QJSEngine 引擎插槽已隔离在 `protocol/script`，v2.0 扩 API 面不动架构 |
| 002 网络通道 | `core/serial/` 之上抽象 `IDataChannel` 接口，TCP/UDP 实现时新增 `core/net/` |
| 003 控制面板 | ChannelStore 双向预留"下行命令"消息接口 |
| 004 多会话标签 | 前端会话对象化（Session 类），GUI 标签页 = 多实例，core 无需改动 |
| 007 跨平台 | `platform/` 目录 + 扫描机制（第 9 节），CMake 顶层按平台条件编译 |
| 009 安装包 | v1.0 仅便携 zip；打包脚本目录 `packaging/` 预留，NSIS 时新增即可 |

## 12. 开放问题（已全部关闭，2026-11-16 Grayson 批准全部 4 项决议）

1. ~~PRD 变更申请：帧尾更正~~ 已批准，PRD F-03 已更正为 `00 00 80 7F`。
2. ~~Qwt License 核对~~ 已完成并回填第 1、5 节，Qwt 定稿采用。
3. ~~list-ports 子命令~~ 已批准，CLI 契约（第 6 节）已更新。
4. ~~安装包形态~~ 已批准：v1.0 仅便携 zip，NSIS 进 PRD TODO-009；DoD 交付物与 4.3 节同步修订（DoD v1.1）。
