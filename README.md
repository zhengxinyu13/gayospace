# GayoSpace

[English](#about) | [中文](#关于)

> An open-source serial debugging tool for embedded engineers: real-time multi-channel waveforms, JustFloat/FireWater protocols, custom JS protocol parsing, and a CLI for automation. Built by an embedded engineer, for embedded engineers.

![Status](https://img.shields.io/badge/status-in%20design-blue) ![License](https://img.shields.io/badge/license-MIT-green) ![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-lightgrey)

## About

GayoSpace is a serial debugging tool with real-time waveform plotting, built with C++ / Qt6. It exists because the author needed a comfortable, free, scriptable serial + oscilloscope tool for day-to-day embedded work — especially PID tuning.

## Features (v1.0 plan)

- **Serial I/O** — hot-plug detection, full port configuration, Text/Hex modes, send history, scheduled sending, auto-reconnect, RX/TX statistics, logging
- **Data engine** — JustFloat & FireWater protocols, plus custom protocols defined in JavaScript (QJSEngine)
- **Real-time waveforms** — multi-channel plotting built for smooth PID tuning sessions (zoom / pan / pause / channel toggle)
- **CLI** — headless mode for data capture and scripted sends, with JSON/CSV output and script-friendly exit codes
- **Session persistence** — port & protocol settings restored across launches

## Roadmap

See [docs/PRD.md](docs/PRD.md) for the full product requirements, including the TODO pool (network channels, control panel, multi-session tabs, macOS/Linux support, and more).

## Status

In design phase — PRD under review. Coding starts after the PRD, DoD and technical specification are finalized.

## Build

Toolchain (planned): C++ / Qt6 / CMake on Windows. Build instructions will be added once the project scaffolds.

## License

[MIT](LICENSE)

---

## 关于

GayoSpace 是一款带实时波形绘制的串口调试工具，使用 C++ / Qt6 构建。它的诞生很简单：作者在日常嵌入式开发（尤其是 PID 调参）中，一直缺一把趁手的、免费可扩展的"串口 + 示波器"工具，于是决定自己写一个。

## 功能规划（v1.0）

- **串口收发** — 热插拔检测、完整端口配置、Text/Hex 双模式、发送历史、定时发送、自动重连、收发统计、日志保存
- **数据引擎** — JustFloat 与 FireWater 协议，另支持用 JavaScript（QJSEngine）自定义解析协议
- **实时波形** — 多通道曲线绘制，为流畅的 PID 调参场景设计（缩放 / 平移 / 暂停 / 通道开关）
- **CLI** — 无界面模式，支持数据采集与脚本化发送，JSON/CSV 输出、退出码可判定，方便 Agent 与自动化调用
- **会话记忆** — 串口与协议配置跨启动恢复

## 路线图

完整产品需求与 TODO 池（网络通道、控制面板、多会话标签页、macOS/Linux 支持等）见 [docs/PRD.md](docs/PRD.md)。

## 当前状态

设计阶段——PRD 评审中，PRD / DoD / 技术方案定稿后开始编码。

## 构建

工具链（规划）：Windows 下 C++ / Qt6 / CMake。工程搭建后补充构建说明。

## 许可证

[MIT](LICENSE)
