# Changelog

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循 [SemVer](https://semver.org/lang/zh-CN/)。
本项目尚无代码发布，以下为文档阶段变更记录（对应 AGENTS.md 第十二节变更管理）。

## [未发布]

### 第 1 轮全面评审修订（R-01～R-28，评审见本地 CODE-REVIEW.md；批准人：Grayson，日期 2026-11-18）

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
