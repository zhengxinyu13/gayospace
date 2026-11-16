# Changelog

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循 [SemVer](https://semver.org/lang/zh-CN/)。
本项目尚无代码发布，以下为文档阶段变更记录（对应 AGENTS.md 第十二节变更管理）。

## [未发布]

### 文档阶段变更（批准人：Grayson，日期 2026-11-16）

- 2026-11-16 PRD 定稿为共识版 v1.0（含 CLI 需求 F-07、跨平台硬约束、TODO 池 8 条）。
- 2026-11-16 DoD 定稿为共识版 v1.0 → v1.1（v0.2 补 10 项 review 缺口，v0.3 细化 4 项，v1.1 随 TS 决议修订发布形态）。
- 2026-11-16 TS-SW 草稿 v0.1：架构分层、线程模型、协议引擎（JustFloat/FireWater 官方原文核对）、波形渲染选型（Qwt，License 已核对）、CLI 输出契约、平台层与 Win32 扫描规则、测试策略。
- 2026-11-16 【变更】PRD F-03 JustFloat 帧尾由草稿占位值更正为官方值 `00 00 80 7F`（影响：PRD F-03、TS-SW 4.1；不影响已发布设备——无）。
- 2026-11-16 【变更】发布形态 v1.0 改为仅便携 zip，NSIS 安装包进 PRD TODO-009（影响：PRD 5 节、DoD 1/4.3 节；无已发布设备）。
- 2026-11-16 【变更】CLI 新增 `list-ports` 子命令（影响：TS-SW 6 节、F-07 实现范围；无已发布设备）。
- 2026-11-16 【需求】GUI 视觉规范定为 Glassmorphism 真玻璃拟态（无边框自绘标题栏、抓桌面模糊背景、玻璃浓度可调、性能模式降级），TS-SW 新增 5.1 节（Grayson 指定，参照其 git-helper 项目实现）。
