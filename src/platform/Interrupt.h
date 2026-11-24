// 控制台中断处理器收敛层（TS-SW §11：平台 API 唯一允许目录；批次② Ctrl+C → 130）。
// 契约：安装后收到 Ctrl+C / SIGINT / SIGTERM 时调用 callback 恰好一次语义（重复触发
// 也可能多次调用——callback 必须幂等且 async-signal-safe，只允许写 std::atomic 等
// 无锁操作，禁止 printf/堆分配/锁）。
#pragma once

namespace gayospace::platform {

using InterruptCallback = void (*)();

// 安装处理器。返回 false = 平台安装失败（调用方自行决定降级策略，如仅靠 duration 超时退出）。
[[nodiscard]] bool installInterruptHandler(InterruptCallback callback);

} // namespace gayospace::platform
