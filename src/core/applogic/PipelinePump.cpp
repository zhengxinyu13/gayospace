// 管道解析泵实现（语义见头文件与 tests/unit/test_pipeline_pump.cpp 用例注释）。
#include "applogic/PipelinePump.h"

#include <algorithm>
#include <span>
#include <utility>

namespace gayospace::applogic {

PipelinePump::PipelinePump(pipeline::SpscRingBuffer& ring,
    std::unique_ptr<protocol::IProtocolParser> parser, std::uint8_t initialChCount,
    std::uint64_t storeCapacityFrames)
    : ring_(ring),
      parser_(std::move(parser)),
      storeCapacityFrames_(storeCapacityFrames)
{
    rebuildStore(initialChCount);
}

void PipelinePump::rebuildStore(std::uint8_t chCount)
{
    // TS §3.3：ch_count 变化 → 重建列布局，旧数据丢弃（ChannelStore 新实例从 frameIndex 0 起算）
    store_ = std::make_unique<pipeline::ChannelStore>(
        chCount == 0 ? 1 : chCount, storeCapacityFrames_);
    ++storeRebuilds_;
}

std::size_t PipelinePump::pumpOnce(std::size_t kMax)
{
    // ① 批次：beginBatch → copyBatch → feed → publish；失败路径 cancelBatch 归还 owner
    //    （ok=false 时 beginBatch 内部已归还、cancelBatch 为 no-op——统一调用防漏）
    auto batch = ring_.beginBatch(kMax);
    if (batch.ok) {
        if (batchBuf_.size() < batch.size) {
            batchBuf_.resize(batch.size);
        }
        const auto n = ring_.copyBatch(batch, batchBuf_.data(), batchBuf_.size());
        parser_->feed(std::span<const std::byte>(batchBuf_.data(), n));
        ring_.publish(batch); // R-71 move-only：发布原对象，不拷贝
    } else {
        ring_.cancelBatch(batch);
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
    // TS §4.4 顺序：旧 parser reset 并销毁 → 清空 ring（dropped 累加）→ 重建 store。
    if (parser_) {
        parser_->reset();
    }
    parser_ = std::move(parser);
    ring_.clear();
    rebuildStore(chCount == 0 ? 1 : chCount);
}

PipelineStats PipelinePump::stats() const
{
    const auto ps = parser_->stats();
    PipelineStats s;
    s.rxBytes = ring_.writePos();
    s.droppedBytes = ring_.droppedBytes();
    s.rxDropped = ring_.rxDropped();
    s.framesOk = ps.frames_ok;
    s.framesBad = ps.frames_bad;
    s.parseErrors = ps.parse_errors;
    s.chCount = store_->channelCount();
    s.frames = store_->frameIndex();
    s.storeRebuilds = storeRebuilds_;
    return s;
}

} // namespace gayospace::applogic
