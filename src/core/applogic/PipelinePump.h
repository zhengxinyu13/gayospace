// 管道解析泵（胶水层核心，第 21 轮评审 §2.1；TS-SW §3.2/§3.3/§4.4）。
// 单线程步进类（非 QObject）：把 "ring 读侧 → parser → store" 接成一条数据链。
// 线程封装（等待/唤醒）在 SerialWorker/PipelineThread 保持很薄，单测用 pumpOnce/drain
// 显式步进驱动——不碰真串口、不碰真线程（评审 §2.2.4：避免 flaky）。
//
// 批次纪律（R-71 move-only）：beginBatch → copyBatch → publish；ok=false（owner 争用失败
// 或缓冲空，owner 已被 beginBatch 内部归还）→ cancelBatch 为 no-op 安全调用，统一调用
// 以保证任何失败路径都归还 owner。
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "applogic/PipelineStats.h"
#include "pipeline/ChannelStore.h"
#include "pipeline/SpscRingBuffer.h"
#include "protocol/IProtocolParser.h"

namespace gayospace::applogic {

class PipelinePump {
public:
    // initialChCount：初始 store 通道数（用户配置 / CLI --channels；动态协议首帧即触发重建）
    PipelinePump(pipeline::SpscRingBuffer& ring, std::unique_ptr<protocol::IProtocolParser> parser,
        std::uint8_t initialChCount, std::uint64_t storeCapacityFrames);

    // 步进一批：有已发布字节 → 拷贝 → parser.feed → 发布；随后把 parser 产出的帧全部
    // 写入 store。ParseFrame.ch_count 与当前 store 通道数不同 → 按 TS §3.3 重建列布局
    // （旧数据丢弃，storeRebuilds++）。返回本次写入 store 的帧数。
    std::size_t pumpOnce(std::size_t kMax = 4096);

    // 排空：循环 pumpOnce 直到 ring 无新批次且 parser 无待产帧（停止前/测试用）。
    std::size_t drain(std::size_t kMax = 4096);

    // 协议切换（TS §4.4）：换 parser（旧 parser reset 后析构）→ ring.clear()（未读字节
    // 计入 droppedBytes）→ store 按新 chCount 重建。调用前提：上层已按 §4.4 时序暂停
    // 解析线程与串口写入（本方法非并发安全）。
    void switchParser(std::unique_ptr<protocol::IProtocolParser> parser, std::uint8_t chCount);

    [[nodiscard]] PipelineStats stats() const;
    [[nodiscard]] pipeline::ChannelStore& store() { return *store_; }
    [[nodiscard]] std::uint32_t storeRebuilds() const noexcept { return storeRebuilds_; }

private:
    void rebuildStore(std::uint8_t chCount);
    void refreshCachedStats(); // 把 parser/store 计数刷进原子缓存（仅 pump 线程调用）

    pipeline::SpscRingBuffer& ring_;
    std::unique_ptr<protocol::IProtocolParser> parser_;
    std::unique_ptr<pipeline::ChannelStore> store_;
    std::uint64_t storeCapacityFrames_;
    std::uint32_t storeRebuilds_ = 0;
    std::vector<std::byte> batchBuf_; // 批次拷贝缓冲（跨 pumpOnce 复用）

    // R-85（第 22 轮评审）：stats() 可能被 CLI/GUI 线程并发调用——缓存式原子快照，
    // stats() 只读这些原子 + ring 自身的原子计数，绝不解引用 parser_/store_
    // （store_ 会被 rebuildStore 替换、parser stats 是普通结构，直接读会 data race/UAF）。
    // 刷新点：pumpOnce 结尾与 switchParser 结尾（均在 pump 线程内）。
    std::atomic<std::uint64_t> cachedFramesOk_{0};
    std::atomic<std::uint64_t> cachedFramesBad_{0};
    std::atomic<std::uint64_t> cachedParseErrors_{0};
    std::atomic<std::uint64_t> cachedFrames_{0};
    std::atomic<std::uint32_t> cachedChCount_{0};
    std::atomic<std::uint32_t> cachedStoreRebuilds_{0};
};

} // namespace gayospace::applogic
