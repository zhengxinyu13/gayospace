# Changelog

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循 [SemVer](https://semver.org/lang/zh-CN/)。
本项目尚无代码发布，以下为文档阶段变更记录（对应 AGENTS.md 第十二节变更管理）。

## [未发布]

### 管道并发契约定稿合并（PR #4；批准人：Grayson，日期 2026-11-20）

- 2026-11-20 【合并】docs/ts-pipeline-concurrency → main 经 PR #4 合入（merge 68a1eb6）；**main CI 全绿（run https://github.com/zhengxinyu13/gayospace/actions/runs/44718223651）**——R-33/R-34 管道并发契约随合并关闭，连同第 10~12 轮评审项 R-59（owner CAS 串行化）、R-60（容量口径）、R-61（复测前 acquire fence）、R-62（帧级单索引）、R-63（版本状态）、R-64（readPos CAS-max）、R-65（writePos 读侧 acquire / 整块边界校验 / 措辞）全部闭环。`feature/pipeline-core` 自本条起按契约开工（测试先行，矩阵 14 项，core/pipeline 覆盖率门槛 80%）。

### 管道并发契约定稿（docs/ts-pipeline-concurrency PR；批准人：Grayson，日期 2026-11-19）

- 2026-11-20 【修订】第 12 轮评审条件批准（R-59/R-61/R-64 设计闭合），合并前补 R-65 三处契约细节：`writePos` 补读侧 acquire 内存序 + 批次 k 来源改临界区内 `k = min(k_max, writePos − readPos)`（不得拷贝未发布区间）；写侧未满直写改**整块边界校验**（`writePos + len − capacity ≤ readPos`，仅校验起始槽对多字节块不够）；"无锁"措辞改"写侧无阻塞、读侧有界自旋"（owner 非严格 lock-free）。测试矩阵扩至 14 项（#14 块尾边界值）。影响：docs/ts-pipeline-concurrency.md v1.2、TS-SW v1.3（§3.2）；协议与管道代码未开工，无已发布设备。
- 2026-11-20 【修订】第 11 轮评审（R-59 复审未闭合、R-64 新增、R-61 论证方向修正）：SPSC 引入单写者/单读者 `owner` 原子 CAS（0 空闲/1 读侧拷贝/2 写侧覆盖决策）串行化"写侧覆盖决策"与"读侧 begin+copy"——ceiling 方案存在 Dekker 式 TOCTOU（写侧先读空闲后覆盖，恰好覆盖读侧拷贝起点），owner 互斥消除该窗口且写侧不阻塞（CAS 失败即丢新计 `rx_dropped`）；`readPos` 改双方 CAS-max 推进（R-64：普通 store 会被写侧 CAS 挤回致序号回退）；ChannelStore 复测内存序改"窗口读 → atomic_thread_fence(acquire) → 复测"（原末端 load-acquire 论证方向反了，acquire 不保证其前操作不被后移）；测试矩阵扩至 13 项（#4 重写为 TOCTOU 交错、新增 #12 单调性、#13 fence 评审检查点）。影响：docs/ts-pipeline-concurrency.md v1.1、TS-SW v1.2（§3.2/§3.3/§6）；协议与管道代码未开工，无已发布设备。
- 2026-11-19 【变更】R-33/R-34 定稿（CODE-REVIEW 第 9 轮 §3 第 1 步，第 10 轮 R-59~R-63 修订）：ChannelStore 并发方案三选一定为 `std::atomic_ref<float>`（relaxed）+ `writeIndex` release/acquire + 读侧复测重试（**帧级单索引**保证跨通道同帧对齐，单份存储，内存上界 76.8 MB 不变，废止裸 seqlock 与双缓冲）；SPSC `readInFlight` 定为**批次末序号（ceiling），拷贝前先发布**（R-59：v0.9 起点定义会致拷贝区间被覆盖，data race）；满载两分支——读侧空闲丢最旧（`droppedBytes`）、读侧在飞丢新到（`rx_dropped`），废止"零拷贝"表述统一为拷贝快照；启动未满窗 clamp 到 0（R-60）；CLI JSON stats 同步增 `rx_dropped`。TS-SW 升 v1.1（§3.2/§3.3/§6），设计论证与 11 项测试矩阵见 docs/ts-pipeline-concurrency.md。影响：TS-SW 3/6 节；`feature/pipeline-core` 依此开工（协议与管道代码未开工，无已发布设备）。
- 2026-11-19 【补强】第 9 轮第 0 步测试卫生（chore/test-hygiene PR）：R-57 FakeTransport 加 closeOnOpen（重配回归测试真正覆盖先关旧口路径）；R-58 覆盖率门禁 `_autogen` 排除改按路径组件匹配（自测 11/11）。

### 串口层首批合并（PR #1；批准人：Grayson，日期 2026-11-19）

- 2026-11-19 【合并】feature/serial-core → main 经 PR #1 合入（merge 1bb81f1）；**main CI 全绿（run https://github.com/zhengxinyu13/gayospace/actions/runs/44503338983）**——串口层首批落地：SerialParams 参数校验、SerialErrorKind 领域错误映射（占用不重试）、ReconnectPolicy 退避策略（1s→2s→4s→5s 封顶 ×10）、ISerialTransport 最小传输接口 + QSerialTransport 封装、SerialChannel 四态状态机与自动重连编排；`src/core/serial` 覆盖率 71.98%（阈值 60%），31 Catch2 + 1 Qt 用例全绿。
- 2026-11-19 【里程碑】评审第 5~8 轮闭环：R-46（CMP0135）、R-51~R-56（关闭/重配误触发重连、openFailed 契约统一、断开同步关端口、open 错误信号去重、QSerialTransport 行为测试、端口名 trim）全部修复关闭；R-57/R-58 留下一轮。
- 2026-11-19 【补强】CI 随串口层调整（590d97c、53f74df）：两个 job 的 Qt 安装补 SerialPort 模块；AUTOMOC 生成文件（`*_autogen`）不进覆盖率分母（门禁自测 10/10）。

### 第 3 轮评审合入（本地评审记录，未随仓库发布；批准人：Grayson，日期 2026-11-19）

- 2026-11-19 【合并】bugfix/review-r3-gates → docs/review-r3-sync --no-ff 合入 main（merge 1c6b089 / 1bde1db）；feature/test-framework 确认是 main 祖先后删除远端与本地分支。**main CI 全绿（run https://github.com/zhengxinyu13/gayospace/actions/runs/44477041081）——第 4 轮评审"编码准入"达成，自本条起允许进入编码**（Qwt 透明 spike 仍为 GUI 模块开工前硬前置；R-33/R-34 为管道 PR 验收项）。

### 第 3 轮评审修复（本地评审记录，未随仓库发布；批准人：Grayson，日期 2026-11-19）

- 2026-11-19 【修正】R-41 覆盖率跨模块重复行按 (文件, 行号) 去重取 max 命中（静态库被多测试 exe 链入时原口径会重复计数→假红）；R-43 src/core/ 根级源文件归入 core 桶；门禁自测扩至 9/9。
- 2026-11-19 【补强】R-42 覆盖率门禁自测接入 CI（gate 之前 fail fast，同 R-38 口径）。
- 2026-11-19 【修正】R-44 自有 MSVC 编译选项收敛为 /W4 /WX（/utf-8 /permissive- /Zc:__cplusplus 由 Qt6::Platform interface 提供，去重）。
- 2026-11-19 【修订】R-37 FireWater 前缀判定二次收窄：冒号前片段能被 strtof 完整解析为数字 → 数据；否则 → 前缀剔除（TS-SW §4.2；FireWater 解析器开工前生效）。
- 2026-11-19 【验证】仓库外干净目录构建 C:\GayoSpace-build-clean\r3：自有 target FLAGS = /W4 /WX + Qt 自带三开关各一次，0 warning C，ctest 3/3；**证据入口 = 构建目录内 build-r3.log（R-45：构建日志不再放 %TEMP%，随构建目录留档）**。

### 第 2 轮评审修复与合入（本地评审记录，未随仓库发布；批准人：Grayson，日期 2026-11-18）

- 2026-11-18 【修正】R-29 CI 覆盖率步骤续行+行内注释语法错误（pwsh 解析 4 errors，job 必红）→ 命令改单行；R-38 扫描器自测先于扫描执行（2900f96）。
- 2026-11-18 【修正】R-30 覆盖率门禁改按 class filename 目录聚合（OpenCppCoverage 的 package=模块名，原口径永不生效）；新增自测 7/7（20f13e3）。
- 2026-11-18 【补强】R-31 扫描规则补 TS §11 五符号 + LoadLibrary/ShellExecute/Sleep/#pragma comment，自测扩至 27 项（5992416）。
- 2026-11-18 【修正】R-12 Catch2 依赖锁改 URL tarball + SHA256（GIT_SHALLOW 与 commit hash 组合被 CMake 禁止，全新 configure 必挂）；R-35 删死文件 src/CMakeLists.txt；R-36 版本注释口径对齐 TS（d562153）。
- 2026-11-18 【修正】R-39 悬空引用改"本地评审记录"；R-40 CLI usage --rate 方括号与 CSV t_index 改名规则统一（1756f87）。
- 2026-11-18 【合并】三分支 docs/review-r1-sync → fix/review-r1-ci-gates → fix/review-r1-build --no-ff 合入 main（0 冲突，实测）；chore/local-dev-env 远端分支删除。
- 2026-11-18 【验证】本机仓库外全新目录构建：132 步、/utf-8 /W4 /permissive- /WX 下 0 warning、ctest 3/3、`gayospace-cli --version` = `build clean-4492fd3`；main CI 全绿（run https://github.com/zhengxinyu13/gayospace/actions/runs/44284960092）。

### 第 1 轮全面评审修订（R-01～R-28，本地评审记录，未随仓库发布；批准人：Grayson，日期 2026-11-18）

- 2026-11-18 【变更】R-01 便携路径采用方案 A（Grayson 拍板）：默认便携模式，配置/转储写解压目录内，不可写回落 %LOCALAPPDATA% 并提示（影响：PRD 5 节、TS-SW 7/10 节、DoD 4.3 节；无已发布设备）。
- 2026-11-18 【修正】R-04 七处文档契约漂移同步（CLI 定名 gayospace-cli.exe、app/ 形态、TODO 池 9 条、DoD 文档状态表、版本常量描述、F-07 stdout 收窄、LGPL 许可口径）。
- 2026-11-18 【变更】R-06~R-09 波形数据模型四项：时间基准（t0+index/fs + --rate）、SPSC 并发契约（readInFlight 批次登记）、降采样改每像素桶 min/max、内存上界修正 float/76.8MB（影响：TS-SW 3/4/6/10 节；协议与管道代码未开工）。
- 2026-11-18 【修正】R-13/R-14/R-25/R-26/R-27 CLI 契约落地前提、脚本引擎 spike 与信任边界、FireWater 前缀规则收窄、JustFloat 重同步状态机、崩溃钩子覆盖面。
- 2026-11-18 【修正】R-21/R-22 Qt LTS 论断补出处并新增待核 #6；PRD 悬空引用改为"本地调研资料"。

### 脚手架阶段变更（2026-11-17，对应提交 e38e3cf~aa65904；批准人：Grayson）

- feat(build)：CMake 骨架——Qt 6.8 LTS / C++20 / MSVC 2022 基线，core/cli/gui/platform/app 五层目录，版本编译期注入（e38e3cf）。
- test(framework)：Catch2 v3.7.1（FetchContent）+ Qt Test 冒烟接入，版本注入链路断言（0f68483，DoD 2.1 硬前置）。
- ci：GitHub Actions workflow——构建 + ctest + OpenCppCoverage（Cobertura）+ Win32 词法扫描 + UI 合规字符串检查（4460b45）。
- fix(ci) ×3：install-qt-action 动态解析 6.8 最新补丁与架构名修正；aqt 3.x 语法修正并锁 aqtinstall==3.3.0；强制 MSVC 工具链（msvc-dev-cmd）+ catch_discover_tests 引入（c37c7b5、e41812a、7c90ad0）。
- fix(build)：版本头唯一命名 generated/gayospace_version.h 消除同名遮蔽 + configure 期自检（8a2bdfc）；cli/gui 入口补齐包含（7cf1275）。
- 【里程碑】feature/test-framework 经 Grayson 批准合入 main（merge aa65904），CI 全绿（run 43855830194：3/3 用例通过 + coverage.xml 产出 + 双扫描通过）。

### 文档阶段变更（批准人：Grayson，日期 2026-11-16）

- 2026-11-16 PRD 定稿为共识版 v1.0（含 CLI 需求 F-07、跨平台硬约束、TODO 池 9 条）。
- 2026-11-16 DoD 定稿为共识版 v1.0 → v1.1（v0.2 补 10 项 review 缺口，v0.3 细化 4 项，v1.1 随 TS 决议修订发布形态）。
- 2026-11-16 TS-SW 草稿 v0.1：架构分层、线程模型、协议引擎（JustFloat/FireWater 官方原文核对）、波形渲染选型（Qwt，License 已核对）、CLI 输出契约、平台层与 Win32 扫描规则、测试策略。
- 2026-11-16 【变更】PRD F-03 JustFloat 帧尾由草稿占位值更正为官方值 `00 00 80 7F`（影响：PRD F-03、TS-SW 4.1；不影响已发布设备——无）。
- 2026-11-16 【变更】发布形态 v1.0 改为仅便携 zip，NSIS 安装包进 PRD TODO-009（影响：PRD 5 节、DoD 1/4.3 节；无已发布设备）。
- 2026-11-16 【变更】CLI 新增 `list-ports` 子命令（影响：TS-SW 6 节、F-07 实现范围；无已发布设备）。
- 2026-11-16 【需求】GUI 视觉规范定为 Glassmorphism 真玻璃拟态（无边框自绘标题栏、抓桌面模糊背景、玻璃浓度可调、性能模式降级），TS-SW 新增 5.1 节（Grayson 指定，参照其 git-helper 项目实现）。
- 2026-11-16 TS-SW 升 v0.2：依据外部评审（DeepSeek，Grayson 提交）补 6 处阻塞缺口（发送路径 8.1 / 失效模式 7 行逐行对策 10 / ChannelStore 数据模型 3.3 / IProtocolParser 接口 4.0 / 协议热切换跨线程同步 4.4 / 串口重连策略 8.3）及重要与细节项（Qwt 透明 spike、全特效降级路径、CLI --out 必选与退出码边界、接收区显示模型、带宽修正 36kB/s、扫描词法匹配、待核项清单化）；章节重排。
- 2026-11-16 【决策】待核三项关闭（Grayson）：① Qt 锁定 6.8 LTS；② OpenCppCoverage 上游已归档，选项 A——v1.0 沿用、v1.1 评估迁移 VS 内置覆盖率；③ FireWater any: 判定规则经核对与官方一致并回填出处。TS-SW 升 v0.3。
- 2026-11-16 【里程碑】TS-SW 定稿为共识版 v1.0（Grayson 确认）：三项待核关闭，两项（com0com 签名安装、Qwt 透明背景 spike）转入编码阶段执行。至此 PRD v1.0 / DoD v1.1 / TS-SW v1.0 三文档齐备，满足 AGENTS.md 编码前置条件；下一步按 DoD 2.1 先搭测试框架工程。
