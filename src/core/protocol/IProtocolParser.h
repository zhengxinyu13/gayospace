// 协议解析器统一接口（TS-SW §4.0，阻塞项 #4 落实）。
// feed 只积累、pop_frame 只产出——单元测试无需真串口（喂标准字节序列 → 断言 pop 结果）。
#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "protocol/ProtocolTypes.h"

namespace gayospace::protocol {

class IProtocolParser {
public:
    virtual ~IProtocolParser() = default;

    // 喂入到达的原始字节（顺序 = 串口到达顺序）；内部保持跨调用状态
    virtual void feed(std::span<const std::byte> bytes) = 0;

    // 取出一条已解析帧；无完整帧返回 std::nullopt。调用方循环取到空为止
    virtual std::optional<ParseFrame> pop_frame() = 0;

    // 清内部状态并重同步（协议切换/串口重开/通道数变更时由引擎调用，TS §4.4）
    virtual void reset() = 0;

    virtual ParseStats stats() const = 0;
};

} // namespace gayospace::protocol
