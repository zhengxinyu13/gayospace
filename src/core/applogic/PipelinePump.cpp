// 管道解析泵实现（语义见头文件与 tests/unit/test_pipeline_pump.cpp 用例注释）。
#include "applogic/PipelinePump.h"

#include <algorithm>
#include <span>
#include <stdexcept>
#include <utility>

namespace gayospace::applogic {

namespace {

// R-91（第 22 轮评审）：批次 RAII guard——未 publish 的批次析构时归还 owner，
// feed/pop_frame 抛异常（如 bad_alloc）不再卡死 ReaderCopy 状态。
struct BatchGuard {
    BatchGuard(pipeline::SpscRingBuffer& r, pipeline::SpscRingBuffer::Batch& b) noexcept
        : ring(r), batch(b)
    {
    }
    pipeline::SpscRingBuffer& ring;
    pipeline::SpscRingBuffer::Batch& batch;
    bool published = false;
    ~BatchGuard()
    {
        if (!published) {
            ring.cancelBatch(batch);
        }
    }
    BatchGuard(const BatchGuard&) = delete;
    BatchGuard& operator=(const BatchGuard&) = delete;
};

} // namespace

PipelinePump::PipelinePump(pipeline::SpscRingBuffer& ring,
    std::unique_ptr<protocol::IProtocolParser> parser, std::uint8_t initialChCount,
    std::uint64_t storeCapacityFrames)
    : ring_(ring),
      parser_(std::move(parser)),
      storeCapacityFrames_(storeCapacityFrames)
{
    // R-89（第 22 轮评审）：初始通道数上界校验（与 JustFloatParser R-76 同风格，
    // 拒绝非法配置而非静默按 200 通道分配内存）
    if (initialChCount == 0 || initialChCount > protocol::kMaxChannels) {
        throw std::invalid_argument("initialChCount must be in [1, kMaxChannels]");
    }
    rebuildStore(initialChCount);
}

void PipelinePump::rebuildStore(std::uint8_t chCount)
{
    // TS §3.3：ch_count 变化 → 重建列布局，旧数据丢弃（ChannelStore 新实例从 frameIndex 0 起算）。
    // 调用点均在 pump 线程内；防御性 clamp 到 [1, kMaxChannels]（parser 层已保证上界）。
    const auto n = static_cast<std::uint8_t>(std::clamp<std::size_t>(
        chCount, 1, protocol::kMaxChannels));
    store_ = std::make_unique<pipeline::ChannelStore>(n, storeCapacityFrames_);
    ++storeRebuilds_;
    cachedChCount_.store(n, std::memory_order_relaxed);
    cachedStoreRebuilds_.store(storeRebuilds_, std::memory_order_relaxed);
    cachedFrames_.store(0, std::memory_order_relaxed);
}

std::size_t PipelinePump::pumpOnce(std::size_t kMax)
{
    // ① 批次：beginBatch → copyBatch → feed → publish；失败路径统一 cancelBatch 归还 owner
    //    （ok=false 时 beginBatch 内部已归还、cancelBatch 为 no-op；异常路径由 BatchGuard 兜底）
    auto batch = ring_.beginBatch(kMax);
    BatchGuard guard{ring_, batch};    if (batch.ok) {
        if (batchBuf_.size() < batch.size) {
            batchBuf_.resize(batch.size);
        }
        const auto n = ring_.copyBatch(batch, batchBuf_.data(), batchBuf_.size());
        parser_->feed(std::span<const std::byte>(batchBuf_.data(), n));
        ring_.publish(batch); // R-71 move-only：发布原对象，不拷贝
        guard.published = true;
    } else {
        ring_.cancelBatch(batch);
        guard.published = true; // cancelBatch 已处理，guard 不再重复调用
    }

    // ② 帧：parser 产出 → store；ch_count 变化 → 重建（TS §3.3）
    std::size_t framesWritten = 0;
    while (auto f = parser_->pop_frame()) {
        if (f->ch_count == 0) {
            continue; // 防御：0 通道帧无意义
        }
        if (f->ch_count != store_->channelCount()) {
            rebuildStore(f->ch_count);
        }
        store_->writeFrame(f->ch.data());
        ++framesWritten;
    }

    refreshCachedStats();
    return framesWritten;
}

std::size_t PipelinePump::drain(std::size_t kMax)
{
    // 排空：ring 无新批次（writePos == readPos）且 parser 无待产帧时收尾。
    // 最后一轮 pumpOnce 仍要执行一次以 flush parser 内部残帧（feed 与产出解耦）。
    std::size_t total = 0;
    for (;;) {
        const bool ringEmpty = ring_.writePos() == ring_.readPos();
        const auto frames = pumpOnce(kMax);
        total += frames;
        if (ringEmpty && frames == 0) {
            return total;
        }
    }
}

void PipelinePump::switchParser(std::unique_ptr<protocol::IProtocolParser> parser, std::uint8_t chCount)
{
    // TS §4.4 顺序（R-87，第 22 轮评审按契约字面序调换）：旧 parser reset 并销毁
    // → 清空 ring（dropped 累加）→ 安装新 parser → 重建 store。
    // 调用前提（R-87 后半）：owner 必须先停止解析线程（PipelineThread::stop + join）
    // 与串口写入后再调用——运行中误调属未定义；TS §4.4 的"命令邮箱"实现挂 GUI 接线前。
    if (chCount == 0 || chCount > protocol::kMaxChannels) {
        throw std::invalid_argument("chCount must be in [1, kMaxChannels]");
    }
    if (parser_) {
        parser_->reset();
    }
    parser_.reset();
    ring_.clear();
    parser_ = std::move(parser);
    rebuildStore(chCount);
    refreshCachedStats();
}

void PipelinePump::refreshCachedStats()
{
    // 仅 pump 线程调用：把 parser/store 计数刷进原子缓存（R-85）。
    const auto ps = parser_->stats();
    cachedFramesOk_.store(ps.frames_ok, std::memory_order_relaxed);
    cachedFramesBad_.store(ps.frames_bad, std::memory_order_relaxed);
    cachedParseErrors_.store(ps.parse_errors, std::memory_order_relaxed);
    cachedFrames_.store(store_->frameIndex(), std::memory_order_relaxed);
}

PipelineStats PipelinePump::stats() const
{
    // R-85：跨线程只读原子缓存 + ring 自身原子计数，不解引用 parser_/store_。
    // 弱一致口径（各计数单调递增，快照间允许微小偏差，对 CLI/GUI stats 足够）。
    PipelineStats s;
    s.rxBytes = ring_.writePos();
    s.droppedBytes = ring_.droppedBytes();
    s.rxDropped = ring_.rxDropped();
    s.framesOk = cachedFramesOk_.load(std::memory_order_relaxed);
    s.framesBad = cachedFramesBad_.load(std::memory_order_relaxed);
    s.parseErrors = cachedParseErrors_.load(std::memory_order_relaxed);
    s.chCount = cachedChCount_.load(std::memory_order_relaxed);
    s.frames = cachedFrames_.load(std::memory_order_relaxed);
    s.storeRebuilds = cachedStoreRebuilds_.load(std::memory_order_relaxed);
    return s;
}

} // namespace gayospace::applogic
