// 自动重连退避策略（TS-SW 第 8.3 节：指数退避 1s → 2s → 4s → 5s（封顶）× 共 10 次）。
// 纯逻辑类，无 Qt 依赖——时序参数可注入，单测用微秒级梯度驱动全流程。
#pragma once

#include <chrono>
#include <vector>

namespace gayospace::serial {

struct ReconnectPolicy {
    // 退避梯度：第 1/2/3 次重试前分别等待 stepDelays 的第 0/1/2 项，
    // 第 4 次起固定 capDelay（即 1s→2s→4s→5s 封顶）。
    std::vector<std::chrono::milliseconds> stepDelays{
        std::chrono::milliseconds{1000}, std::chrono::milliseconds{2000},
        std::chrono::milliseconds{4000}};
    std::chrono::milliseconds capDelay{5000};
    int maxAttempts = 10; // 共 10 次重连尝试（TS §8.3），耗尽即进入"重连失败"终态

    // attempt 为 1 起始的重试序号；返回该次尝试前的等待时长。
    // attempt 超出梯度时取封顶值；attempt <= 0 或超出 maxAttempts 返回 0（不应发生，防御）。
    [[nodiscard]] std::chrono::milliseconds delayBefore(int attempt) const
    {
        if (attempt <= 0 || attempt > maxAttempts) {
            return std::chrono::milliseconds{0};
        }
        const auto idx = static_cast<size_t>(attempt - 1);
        if (idx < stepDelays.size()) {
            return stepDelays[idx];
        }
        return capDelay;
    }

    [[nodiscard]] bool exhausted(int attempt) const { return attempt >= maxAttempts; }

    // TS §8.3 默认策略：1s → 2s → 4s → 5s（封顶）× 10 次。
    [[nodiscard]] static ReconnectPolicy defaultPolicy() { return ReconnectPolicy{}; }
};

} // namespace gayospace::serial
