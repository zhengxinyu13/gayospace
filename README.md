# GayoSpace

[English](#about) | [中文](#关于)

> An open-source serial debugging tool for embedded engineers: real-time multi-channel waveforms, JustFloat/FireWater protocols, custom JS protocol parsing, and a CLI for automation. Built by an embedded engineer, for embedded engineers.

![Status](https://img.shields.io/badge/status-scaffold%20v0.1-blue) ![License](https://img.shields.io/badge/license-MIT-green) ![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-lightgrey)

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

v0.1 scaffold: build system, test framework (Catch2 v3 + Qt Test), CI and static gates are in place; PRD / DoD / TS-SW finalized. Business coding starts next.

## Build

### Prerequisites

- Windows 10/11 x64
- Visual Studio 2022 Build Tools (C++ workload, MSVC 2022 x64) — [download](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022)
- CMake >= 3.21, Ninja
- Qt 6.8 LTS (MSVC 2022 x64) — via the [official installer](https://www.qt.io/download-qt-installer) or `aqt`:

```powershell
pip install aqtinstall==3.3.0
python -m aqt install-qt windows desktop 6.8 win64_msvc2022_64 --outputdir C:\Qt
```

### Configure & build

The `dev` preset reads the Qt install path from the `QT_ROOT_DIR` environment variable:

```powershell
$env:QT_ROOT_DIR = "C:\Qt\6.8.3\msvc2022_64"   # adjust to your Qt install
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Artifacts: `GayoSpace.exe` (GUI) and `gayospace-cli.exe` (CLI) under the build directory. `--version` prints the injected SemVer and build id.

Notes:
- First configure fetches Catch2 (pinned to a commit SHA) from GitHub — network required once.
- CI builds with the `ci` preset on `windows-latest`; see [.github/workflows/ci.yml](.github/workflows/ci.yml).
- macOS/Linux: the core layer is kept platform-free and compile-tested on Linux in CI; full port is on the roadmap (TODO-007).

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

v0.1 脚手架阶段：构建系统、测试框架（Catch2 v3 + Qt Test）、CI 与静态门禁已就位；PRD / DoD / TS-SW 已定稿，业务编码即将开始。

## 构建

### 环境准备

- Windows 10/11 x64
- Visual Studio 2022 Build Tools（C++ 工作负载，MSVC 2022 x64）——[下载](https://visualstudio.microsoft.com/zh-cn/downloads/#build-tools-for-visual-studio-2022)
- CMake >= 3.21、Ninja
- Qt 6.8 LTS（MSVC 2022 x64）——官方安装器或 `aqt`：

```powershell
pip install aqtinstall==3.3.0
python -m aqt install-qt windows desktop 6.8 win64_msvc2022_64 --outputdir C:\Qt
```

### 配置与构建

`dev` preset 从环境变量 `QT_ROOT_DIR` 读取 Qt 安装路径：

```powershell
$env:QT_ROOT_DIR = "C:\Qt\6.8.3\msvc2022_64"   # 按实际安装路径调整
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

产物：构建目录下的 `GayoSpace.exe`（GUI）与 `gayospace-cli.exe`（CLI）。`--version` 可读出编译期注入的 SemVer 版本与构建 ID。

说明：
- 首次 configure 会从 GitHub 拉取 Catch2（已锁定 commit SHA），需要联网一次。
- CI 在 `windows-latest` 上以 `ci` preset 构建，见 [.github/workflows/ci.yml](.github/workflows/ci.yml)。
- macOS/Linux：core 层保持无平台依赖并在 Linux CI 上编译验证；完整移植在路线图中（TODO-007）。

## 许可证

[MIT](LICENSE)
