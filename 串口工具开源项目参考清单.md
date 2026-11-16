# 串口工具开源项目参考清单

> 整理范围：GitHub / Gitee 上主流及有设计参考价值的串口相关开源项目。GitHub 与 Gitee 重叠的项目以 GitHub 链接为主，Gitee 独有或首发项目单独标注。

## 一、速查总表

| 项目                    | 形态               | 主要技术栈                                                | 平台                               |
| ----------------------- | ------------------ | --------------------------------------------------------- | ---------------------------------- |
| LLCOM                   | 桌面 GUI           | C# / .NET，Lua 脚本引擎                                   | Windows                            |
| Wu.CommTool             | 桌面 GUI           | C# / WPF / Prism / MaterialDesign / HandyControl / .NET 6 | Windows                            |
| BYSerial                | 桌面 GUI           | C# / WPF / HandyControl / LiveCharts / MQTTnet            | Windows                            |
| OhmyPort                | 桌面 GUI           | C# / WPF / .NET 10，Core 分层                             | Windows                            |
| KoboldCom               | 通信类库           | C# / .NET 8，协议解析框架                                 | 跨平台（库）                       |
| Serial Mate             | 跨平台 GUI         | Go / Wails / Vue 3                                        | Win / macOS / Linux                |
| RSerial Debug Assistant | 跨平台 GUI         | Tauri 2.0 / React 18 / Rust                               | Win / macOS / Linux                |
| xTools                  | 跨平台 GUI         | C++ / Qt6，Lua + JavaScript 脚本                          | Win / Linux / macOS / Android      |
| serial_debug_tool       | 桌面 GUI           | C++20 / Qt6，JavaScript 引擎，ECharts                     | Windows 为主                       |
| ByteSpace               | 桌面 GUI           | C++ / Qt6                                                 | 跨平台                             |
| COMTool                 | 跨平台 GUI         | Python                                                    | Win / Linux / macOS / Raspberry Pi |
| AFCOM                   | 跨平台 GUI         | Python / PySide6                                          | Win / Linux / macOS                |
| tio                     | CLI                | C，Lua 脚本                                               | Linux / macOS                      |
| yap                     | TUI                | Rust / Ratatui                                            | 跨平台                             |
| Serial-Agent            | VS Code 扩展 + MCP | TypeScript Monorepo                                       | VS Code（本地）                    |
| Bus Studio              | Web + 桌面         | Web Serial API                                            | 浏览器 / 桌面                      |
| usb-serial-for-android  | Android 库         | Java，Android USB Host API                                | Android                            |


## 二、.NET / WPF / C# 系（Windows 桌面主力）

### 1. LLCOM
- **链接**：https://github.com/chenxuuu/llcom
- **技术栈**：C# / .NET；Lua 脚本引擎（xlua）；Apache 2.0
- **定位**：功能密度最高的 .NET 串口工具之一。除常规收发外，支持 Lua 自动化脚本、串口曲线、TCP / UDP / MQTT 测试、编码转换与乱码恢复，并可通过 Lua 调用 C# 接口完成任意扩展。
- **参考点**：脚本扩展架构、多协议集成方式、Lua 协程任务管理。

### 2. Wu.CommTool
- **链接**：https://github.com/Monika1313/Wu.CommTool
- **技术栈**：C# / WPF / Prism / MaterialDesign / HandyControl / .NET 6
- **定位**：界面现代感较强的通讯调试工具，原生支持 Modbus RTU 自动帧解析、智能防粘包、MQTT Server / Client、TCP / UDP 调试。
- **参考点**：WPF 现代 UI 组件选型（MaterialDesign + HandyControl 组合）、Prism 模块化结构、Modbus RTU 自动应答实现。

### 3. BYSerial（Gitee 首发）
- **链接**：https://gitee.com/LvYiWuHen/byserial
- **技术栈**：C# / WPF；HandyControl、Newtonsoft.Json、LiveCharts、IoTClient、MQTTnet
- **定位**：MIT 许可的 Windows GUI 串口工具，支持中英文切换、串口 / TCP 调试、接收数据曲线，以及 Modbus RTU/ASCII/TCP、多家 PLC 协议和 MQTT 客户端。
- **参考点**：MIT 友好的许可、工业协议集成广度、LiveCharts 曲线方案。

### 4. OhmyPort
- **链接**：https://github.com/Joke66/ohmy-port
- **技术栈**：C# / WPF / .NET 10；`OhmyPort.Core` 为无 UI 依赖的纯逻辑层，xUnit 单元测试
- **定位**：Python + PySide6 版本的 C# / WPF 重写分支，在保留功能的同时追求更小体积和更快启动。覆盖协议帧解析（SUM8 / XOR8 / CRC16-Modbus）、日志过滤、实时曲线、多会话标签页。
- **参考点**：Core 分层与单元测试结构、协议帧解析框架、日志系统设计。

### 5. KoboldCom
- **链接**：https://github.com/PeterRock/KoboldCom
- **技术栈**：C# / .NET 8；`System.IO.Ports`
- **定位**：商业级 C# 串口通信**类库**（非 GUI 助手），核心能力是按协议切分数据帧并映射到数据模型，一个端口可同时注册多个协议解析器。
- **参考点**：通讯层与 UI 解耦、多协议并行解析、事件驱动架构。


## 三、跨平台现代技术栈（Go / Rust / Tauri）

### 6. Serial Mate
- **链接**：https://github.com/TheWinds071/serial-mate
- **技术栈**：Go（Wails）+ Vue 3
- **定位**：现代化串口调试助手，莫兰迪配色 + 动态主题引擎，支持串口 / TCP / UDP / RTT，内置实时曲线（字符串协议与 Float32 小端 Hex 协议两种格式），支持自动更新。
- **参考点**：Go + Wails + Vue 的轻量跨平台方案、动态主题实现、实时曲线协议设计。

### 7. RSerial Debug Assistant
- **链接**：https://github.com/Gyanano/RSerialDebugAssistant
- **技术栈**：Tauri 2.0 / React 18 / Rust；shadcn UI
- **定位**：专业级跨平台串口调试器，功能覆盖智能端口检测、Text / Hex 收发、多种校验和（XOR / ADD8 / CRC8 / CRC16 / CCITT-CRC16）、超时 / 分隔符 / 组合断帧、快速命令批量执行、自动更新。
- **参考点**：Tauri 在串口场景的工程实践、校验和与断帧的完整实现、React 组件化调试界面。

### 8. yap
- **链接**：https://github.com/nullstalgia/yap
- **技术栈**：Rust / Ratatui（TUI）
- **定位**：友好的串口终端，支持智能自动重连、ANSI 着色规则、ESP32 固件烧录集成、defmt 帧解码、宏与可配置键绑定。
- **参考点**：串口终端与嵌入式工作流（烧录、日志解码）的深度结合、TUI 交互设计。

### 9. Serial-Agent
- **链接**：https://github.com/Rance-OwO/Serial-Agent
- **技术栈**：TypeScript Monorepo；VS Code 扩展 + MCP Server + 本地 Bridge
- **定位**：面向嵌入式开发的 AI 串口调试平台，将串口监视、HEX 收发、日志缓冲与自动重连封装为 VS Code 扩展，并通过 MCP 把串口能力暴露给 AI，形成“编译 / 烧录 → 读日志 → 分析 → 辅助修复”的闭环。
- **参考点**：串口工具与 AI / MCP 集成的完整链路、Monorepo 工程组织、本地安全设计（127.0.0.1 + Token）。


## 四、C++ / Qt 系

### 10. xTools
- **链接**：https://github.com/x-tools-author/x-tools
- **技术栈**：C++ / Qt6；Lua + JavaScript 脚本扩展；支持 SerialPort、HID、BLE、UDP、TCP、WebSocket、Modbus、CAN Bus、MQTT、CoAP 等
- **定位**：功能覆盖极广的 Qt 调试工具集，具备脚本扩展和图表视图能力，跨 Windows / Linux / macOS / Android。
- **参考点**：Qt 多协议统一框架、Lua / JavaScript 双脚本引擎、大规模工具集的模块划分。

### 11. serial_debug_tool
- **链接**：https://github.com/wangxiangyu2020/serial_debug_tool
- **技术栈**：C++20 / Qt6；JavaScript 脚本引擎；ECharts 波形显示
- **定位**：集串口、TCP、Modbus RTU、JavaScript 脚本处理和 ECharts 可视化于一体的通信调试平台。
- **参考点**：Qt6 + C++20 的现代工程实践、JavaScript 脚本引擎集成、ECharts 在桌面端的嵌入方式。

### 12. ByteSpace
- **链接**：https://github.com/ciyeer/ByteSpace
- **技术栈**：C++ / Qt6
- **定位**：基于 Qt6 的串口调试工具，支持波特率、数据位、停止位、校验位、流控配置，具备自动串口检测与热插拔。
- **参考点**：Qt6 串口基础封装的干净实现、热插拔检测。


## 五、Python 系

### 13. COMTool
- **链接**：https://github.com/Neucrack/COMTool（也可搜索 `AIchar/COMTool` 等镜像）
- **技术栈**：Python
- **定位**：跨平台串口 / 网络调试助手，支持插件和二次开发，可在 Windows / Linux / macOS / Raspberry Pi 运行，具备串口自动检测、断线重连、TCP/UDP、终端和图表绘制。
- **参考点**：Python 串口工具的最小可用架构、插件机制、跨 Raspberry Pi 部署。

### 14. AFCOM
- **链接**：https://github.com/mcagriaksoy/Serial-Communication-GUI-Program
- **技术栈**：Python / PySide6
- **定位**：轻量跨平台串口终端，界面简洁，支持完整串口参数、ANSI 颜色、RX/TX 统计和日志保存。
- **参考点**：PySide6 快速构建串口 GUI 的简洁范式，适合原型验证或内部工具。


## 六、CLI / TUI / 服务化

### 15. tio
- **链接**：https://github.com/tio/tio
- **技术栈**：C；Lua 脚本支持
- **定位**：Linux / macOS 下成熟的 CLI 串口设备 I/O 工具，支持配置化连接、Lua 自动化、expect/send 工作流，常用于替代 `screen` 配合 `tmux` 使用。
- **参考点**：CLI 串口工具的配置管理与脚本化设计、单职责工具的产品边界。

### 16. KoboldCom（再次列出，服务化视角）
- 虽为类库，但其“打开串口 → 按协议切帧 → 映射模型”的职责划分，适合作为任何语言串口服务化封装的设计参照。


## 七、Web Serial / 浏览器方案

### 17. Bus Studio
- **链接**：https://github.com/qdsang/bus-studio
- **技术栈**：Web Serial API（Chrome / Edge 等现代浏览器）
- **定位**：同时提供 Web 网页版和桌面版的串口调试工具，支持 UART、蓝牙、WebSocket、ST-Link 等。
- **参考点**：免安装、浏览器即用的串口工具形态，Web Serial 的能力边界与权限模型。


## 八、Android 串口库

### 18. usb-serial-for-android
- **链接**：https://github.com/mik3y/usb-serial-for-android
- **技术栈**：Java；Android USB Host Mode（OTG）
- **定位**：Android 平台 USB 串口通信的事实标准库之一，无需 root、ADK 或特殊内核驱动，支持 CDC、FTDI、Arduino 等多种芯片。
- **参考点**：Android OTG 串口通信的基础接入方式，是任何 Android 串口工具的首选底层依赖。


## 九、按自研目标的项目匹配建议

| 你的目标                   | 优先精读                           | 重点看什么                                                   |
| -------------------------- | ---------------------------------- | ------------------------------------------------------------ |
| Windows 桌面 GUI，功能全面 | **LLCOM** + **OhmyPort**           | LLCOM 的 Lua 扩展与多协议广度；OhmyPort 的 Core 分层与协议帧解析 |
| Windows 桌面 GUI，界面现代 | **Wu.CommTool** + **BYSerial**     | WPF 组件选型、Prism 模块化、工业协议集成                     |
| 跨平台现代 GUI             | **RSerial** + **Serial Mate**      | Tauri + React 的专业调试器范式；Go + Wails + Vue 的轻量路线  |
| Qt 原生 / 高性能桌面       | **xTools** + **serial_debug_tool** | 多协议统一框架、脚本引擎、ECharts 可视化                     |
| Python 快速原型            | **COMTool** + **AFCOM**            | 插件机制与最小可用架构                                       |
| CLI / TUI                  | **tio** + **yap**                  | CLI 配置管理与脚本化；TUI 交互与嵌入式工作流                 |
| 串口 + AI / 自动化         | **Serial-Agent**                   | VS Code 扩展 + MCP + 本地 Bridge 的完整链路                  |
| Web 免安装                 | **Bus Studio**                     | Web Serial API 的工程化落地                                  |
| Android                    | **usb-serial-for-android**         | OTG 串口通信底层库                                           |

**几个模块级的快速索引**：
- **协议帧解析 / 校验**：OhmyPort（SUM8/XOR8/CRC16-Modbus）、RSerial（XOR/ADD8/CRC8/CRC16/CCITT-CRC16 + 断帧策略）、KoboldCom（多协议切帧框架）。
- **插件 / 脚本扩展**：LLCOM（Lua + xlua 调 C#）、xTools（Lua + JavaScript）、COMTool（插件）、serial_debug_tool（JavaScript 引擎）。
- **实时绘图**：Serial Mate（字符串 / Float32 Hex 双协议）、OhmyPort（日志提取曲线）、serial_debug_tool（ECharts）。
- **服务化 / 多语言调用**：Serial-Agent（MCP 暴露串口能力）、KoboldCom（类库级封装）。

---

文档中所有链接均为对应项目的源码仓库地址。若你的目标平台或技术偏好进一步收窄（例如“只做 Windows WPF”或“只做跨平台 Tauri”），可以在此基础上再精选 2～3 个做深度拆解。