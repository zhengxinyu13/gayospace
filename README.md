# GayoSpace

> An open-source serial debugging tool for embedded engineers: real-time multi-channel waveforms, JustFloat/FireWater protocols, custom JS protocol parsing, and a CLI for automation. Built by an embedded engineer, for embedded engineers.

![Status](https://img.shields.io/badge/status-in%20design-blue) ![License](https://img.shields.io/badge/license-MIT-green) ![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-lightgrey)

## About

GayoSpace is a serial debugging tool with real-time waveform plotting, built with C++ / Qt6. It exists because the author needed a comfortable, free, scriptable serial + oscilloscope tool for day-to-day embedded work — especially PID tuning.

## Planned Features (v1.0)

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
