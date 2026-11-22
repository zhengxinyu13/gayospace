// JustFloat 解析器单测（TS-SW §4.1 + 第 17 轮评审 §2.2 测试向量清单）。
// 全部测试向量用 literal std::byte 数组构造，不靠运行时拼（评审 §2.2）。
// 期望行为与实现取舍逐条写在各用例注释里，供评审对账。
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <vector>

#include "protocol/JustFloatParser.h"

using namespace gayospace::protocol;
using Catch::Matchers::WithinRel;

namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();

// 小端 float → 4 字节
std::array<std::byte, 4> f32(float v)
{
    const auto u = std::bit_cast<std::uint32_t>(v);
    return {
        static_cast<std::byte>(u & 0xFFu),
        static_cast<std::byte>((u >> 8) & 0xFFu),
        static_cast<std::byte>((u >> 16) & 0xFFu),
        static_cast<std::byte>((u >> 24) & 0xFFu)};
}

constexpr std::array<std::byte, 4> kTail{
    std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x7F}};

// N 通道一帧：values + 帧尾
std::vector<std::byte> frame(const std::vector<float>& values)
{
    std::vector<std::byte> out;
    for (const float v : values) {
        const auto b = f32(v);
        out.insert(out.end(), b.begin(), b.end());
    }
    out.insert(out.end(), kTail.begin(), kTail.end());
    return out;
}

// 弹出全部帧
std::vector<ParseFrame> drain(IProtocolParser& p)
{
    std::vector<ParseFrame> out;
    while (auto f = p.pop_frame()) {
        out.push_back(*f);
    }
    return out;
}

} // namespace

TEST_CASE("justfloat normal stream yields frames in order", "[protocol][justfloat]")
{
    JustFloatParser p(4);
    auto f1 = frame({1.0F, 2.0F, 3.0F, 4.0F});
    auto f2 = frame({5.0F, 6.0F, 7.0F, 8.0F});
    std::vector<std::byte> stream;
    stream.insert(stream.end(), f1.begin(), f1.end());
    stream.insert(stream.end(), f2.begin(), f2.end());

    p.feed(stream);
    const auto frames = drain(p);

    REQUIRE(frames.size() == 2);
    REQUIRE(frames[0].frame_index == 0);
    REQUIRE(frames[1].frame_index == 1);
    REQUIRE(frames[0].ch_count == 4);
    REQUIRE_THAT(frames[0].ch[0], WithinRel(1.0F));
    REQUIRE_THAT(frames[0].ch[3], WithinRel(4.0F));
    REQUIRE_THAT(frames[1].ch[0], WithinRel(5.0F));
    REQUIRE_THAT(frames[1].ch[3], WithinRel(8.0F));

    const auto st = p.stats();
    REQUIRE(st.frames_ok == 2);
    REQUIRE(st.frames_bad == 0);
    REQUIRE(st.bytes_dropped_sync == 0);
    REQUIRE(st.parse_errors == 0);           // JustFloat 恒 0（ProtocolTypes.h 注释）
}

TEST_CASE("justfloat partial frame waits then completes", "[protocol][justfloat]")
{
    JustFloatParser p(4);
    auto f = frame({1.0F, 2.0F, 3.0F, 4.0F});

    // 先喂半帧（含帧尾前半）——不得产出，不得丢弃（残帧负载已在缓冲）
    const std::size_t half = f.size() / 2;
    p.feed(std::span<const std::byte>(f.data(), half));
    REQUIRE_FALSE(p.pop_frame().has_value());
    REQUIRE(p.stats().bytes_dropped_sync == 0);

    // 补齐后产出完整帧
    p.feed(std::span<const std::byte>(f.data() + half, f.size() - half));
    const auto frames = drain(p);
    REQUIRE(frames.size() == 1);
    REQUIRE_THAT(frames[0].ch[2], WithinRel(3.0F));
    REQUIRE(p.stats().frames_ok == 1);
}

TEST_CASE("justfloat glued frames in one feed both parse", "[protocol][justfloat]")
{
    JustFloatParser p(4);
    auto f1 = frame({1.0F, 2.0F, 3.0F, 4.0F});
    auto f2 = frame({5.0F, 6.0F, 7.0F, 8.0F});
    std::vector<std::byte> stream;           // 两帧连发一次 feed
    stream.insert(stream.end(), f1.begin(), f1.end());
    stream.insert(stream.end(), f2.begin(), f2.end());

    p.feed(stream);
    const auto frames = drain(p);
    REQUIRE(frames.size() == 2);             // 帧距 = 帧长整倍 → 两帧均确认
    REQUIRE_THAT(frames[0].ch[0], WithinRel(1.0F));
    REQUIRE_THAT(frames[1].ch[0], WithinRel(5.0F));
}

TEST_CASE("justfloat plus-inf payload does not corrupt following frames",
    "[protocol][justfloat]")
{
    // +Inf 负载构造帧（帧尾字节在负载中重现，TS §4.1 帧尾歧义）：
    // 负载中的假帧尾因"帧头不在缓冲"（负载不全）被跳过、不计歧义；真·帧尾随后命中且
    // 帧距整倍 → 帧完整确认（+Inf 值原样带出，不污染后续帧）。
    JustFloatParser p(4);
    auto f1 = frame({1.0F, 2.0F, 3.0F, 4.0F});
    auto f2 = frame({5.0F, kInf, 7.0F, 8.0F});
    std::vector<std::byte> stream;
    stream.insert(stream.end(), f1.begin(), f1.end());
    stream.insert(stream.end(), f2.begin(), f2.end());

    p.feed(stream);
    const auto frames = drain(p);

    REQUIRE(frames.size() == 2);
    REQUIRE(std::isinf(frames[1].ch[1]));    // +Inf 通道值原样带出
    REQUIRE_THAT(frames[1].ch[2], WithinRel(7.0F));
    const auto st = p.stats();
    REQUIRE(st.frames_ok == 2);
    REQUIRE(st.frames_bad == 0);
}

TEST_CASE("justfloat wrong channel count keeps counting frames_bad",
    "[protocol][justfloat]")
{
    // 通道数配错（TS §4.0：表现为持续 frames_bad 增长）：实际 2 通道流（帧长 12），
    // parser 配 4（帧长 20）。
    // 行为：首个帧距校验可行的帧尾在第 2 帧尾（tail_end=24 ≥ 帧长 20，frame_start=4）
    // 无锚假确认一次（错位数据，R-26"必然成一帧"），其帧头之前 4 B 错位字节按 R-78
    // 口径计入 dropped；此后每个真帧尾与锚的间距（12 的倍数）对帧长 20 永非整倍
    // → 持续 frames_bad。
    JustFloatParser p(4);
    for (const float base : {1.0F, 3.0F, 5.0F, 7.0F, 9.0F, 11.0F}) {
        auto f = frame({base, base + 1.0F});
        p.feed(f);
    }

    const auto st = p.stats();
    REQUIRE(st.frames_bad >= 2);             // 持续 frames_bad
    REQUIRE(st.frames_ok == 1);              // 仅首个无锚假确认
    REQUIRE(st.bytes_dropped_sync == 4);     // R-78 口径：假确认帧帧头之前的错位字节
}

TEST_CASE("justfloat garbage stream hits buffer cap and counts drops",
    "[protocol][justfloat]")
{
    // 垃圾流（无帧尾）：缓冲上限 4×帧长 兜底，超限丢最旧并计数（评审 §2.2）
    JustFloatParser p(4);                    // 帧长 20，上限 80 字节
    std::vector<std::byte> garbage(4096);
    for (std::size_t i = 0; i < garbage.size(); ++i) {
        garbage[i] = static_cast<std::byte>(i * 7 + 1);
    }
    p.feed(garbage);

    REQUIRE_FALSE(p.pop_frame().has_value());
    REQUIRE(p.stats().bytes_dropped_sync > 0);
    REQUIRE(p.stats().frames_ok == 0);
}

TEST_CASE("justfloat byte-by-byte feed matches whole-block feed", "[protocol][justfloat]")
{
    // 逐字节 feed 与整块 feed 结果一致（评审 §2.2；feed 分块不得改变结果）
    auto f1 = frame({1.0F, 2.0F, 3.0F, 4.0F});
    auto f2 = frame({kInf, 6.0F, 7.0F, 8.0F});
    std::vector<std::byte> stream;
    stream.insert(stream.end(), f1.begin(), f1.end());
    stream.insert(stream.end(), f2.begin(), f2.end());

    JustFloatParser block(4);
    block.feed(stream);
    const auto expected = drain(block);

    JustFloatParser byteByByte(4);
    for (const std::byte b : stream) {
        byteByByte.feed(std::span<const std::byte>(&b, 1));
    }
    const auto actual = drain(byteByByte);

    REQUIRE(actual.size() == expected.size());
    REQUIRE(actual.size() == 2);
    for (std::size_t i = 0; i < expected.size(); ++i) {
        REQUIRE(actual[i].frame_index == expected[i].frame_index);
        REQUIRE(actual[i].ch_count == expected[i].ch_count);
        for (std::size_t c = 0; c < expected[i].ch_count; ++c) {
            REQUIRE(actual[i].ch[c] == expected[i].ch[c]);
        }
    }
    const auto st = byteByByte.stats();
    REQUIRE(st.frames_ok == 2);
    REQUIRE(st.frames_bad == 0);             // 与整块口径一致（f2 负载假帧尾被跳过）
}

TEST_CASE("justfloat realigns after tail desync", "[protocol][justfloat]")
{
    // 帧尾错位后重同步（TS §4.1 测试向量）：负载中 +Inf 的假帧尾（负载不全 → 跳过）
    // 不影响真帧尾的帧距整倍校验，三帧全部按序确认，帧序号连续。
    JustFloatParser p(4);
    auto f1 = frame({1.0F, 2.0F, 3.0F, 4.0F});
    auto f2 = frame({5.0F, kInf, 7.0F, 8.0F});
    auto f3 = frame({9.0F, 10.0F, 11.0F, 12.0F});
    std::vector<std::byte> stream;
    stream.insert(stream.end(), f1.begin(), f1.end());
    stream.insert(stream.end(), f2.begin(), f2.end());
    stream.insert(stream.end(), f3.begin(), f3.end());

    p.feed(stream);
    const auto frames = drain(p);
    REQUIRE(frames.size() == 3);
    REQUIRE_THAT(frames[2].ch[0], WithinRel(9.0F));
    REQUIRE(frames[2].frame_index == 2);
    REQUIRE(p.stats().frames_bad == 0);
}

TEST_CASE("justfloat reset clears state and stats", "[protocol][justfloat]")
{
    JustFloatParser p(4);
    p.feed(frame({1.0F, 2.0F, 3.0F, 4.0F}));
    REQUIRE(drain(p).size() == 1);

    p.reset();
    const auto st = p.stats();
    REQUIRE(st.frames_ok == 0);
    REQUIRE(st.frames_bad == 0);
    REQUIRE(st.bytes_dropped_sync == 0);
    REQUIRE(st.parse_errors == 0);
    REQUIRE_FALSE(p.pop_frame().has_value());

    // reset 后重新锚定：同一条流再次解析
    p.feed(frame({5.0F, 6.0F, 7.0F, 8.0F}));
    const auto frames = drain(p);
    REQUIRE(frames.size() == 1);
    REQUIRE(frames[0].frame_index == 0);     // 帧序号从 0 重新计数
}

TEST_CASE("justfloat parser constructs with configured channel count", "[protocol][justfloat]")
{
    JustFloatParser p2(2);
    auto f = frame({1.5F, 2.5F});
    p2.feed(f);
    const auto frames = drain(p2);
    REQUIRE(frames.size() == 1);
    REQUIRE(frames[0].ch_count == 2);
    REQUIRE_THAT(frames[0].ch[1], WithinRel(2.5F));

    // 32 通道边界：合法（kMaxChannels）
    JustFloatParser p32(32);
    std::vector<float> values32;
    for (std::size_t i = 0; i < 32; ++i) {
        values32.push_back(static_cast<float>(i));
    }
    p32.feed(frame(values32));
    const auto frames32 = drain(p32);
    REQUIRE(frames32.size() == 1);
    REQUIRE(frames32[0].ch_count == 32);
    REQUIRE_THAT(frames32[0].ch[31], WithinRel(31.0F));
}

TEST_CASE("justfloat rejects channel count out of range", "[protocol][justfloat]")
{
    // R-76：0 与 >kMaxChannels 的构造必须显式拒绝（静默钳制会按错误帧长解析，更危险）
    REQUIRE_THROWS_AS(JustFloatParser(0), std::invalid_argument);
    REQUIRE_THROWS_AS(JustFloatParser(33), std::invalid_argument);
    REQUIRE_THROWS_AS(JustFloatParser(255), std::invalid_argument);
}

TEST_CASE("justfloat batch feed of 10 frames yields all without drops",
    "[protocol][justfloat]")
{
    // R-75 验收：一次 feed 超过缓冲上限（4 帧）的批量输入必须全部产出、零丢弃。
    // 旧实现"先截断后解析"会只产出 4 帧、dropped=120。
    JustFloatParser p(4);                    // 帧长 20，上限 80 B
    std::vector<std::byte> stream;
    for (int k = 0; k < 10; ++k) {
        auto f = frame({static_cast<float>(k), 1.0F, 2.0F, 3.0F});
        stream.insert(stream.end(), f.begin(), f.end());
    }
    p.feed(stream);                          // 200 B > 80 B cap

    const auto frames = drain(p);
    REQUIRE(frames.size() == 10);
    REQUIRE_THAT(frames.front().ch[0], WithinRel(0.0F));
    REQUIRE_THAT(frames.back().ch[0], WithinRel(9.0F));
    const auto st = p.stats();
    REQUIRE(st.frames_ok == 10);
    REQUIRE(st.bytes_dropped_sync == 0);
}

TEST_CASE("justfloat batch feed of 100 eight-channel frames yields all",
    "[protocol][justfloat]")
{
    // R-75 验收（8ch 帧长 36 B，100 帧 = 3600 B，一次 feed；真实 8ch@1kHz 16ms 批次即 576 B）
    JustFloatParser p(8);
    std::vector<std::byte> stream;
    for (int k = 0; k < 100; ++k) {
        auto f = frame({static_cast<float>(k), 1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F});
        stream.insert(stream.end(), f.begin(), f.end());
    }
    p.feed(stream);

    const auto frames = drain(p);
    REQUIRE(frames.size() == 100);
    REQUIRE_THAT(frames.back().ch[0], WithinRel(99.0F));
    REQUIRE(frames.back().frame_index == 99);
    const auto st = p.stats();
    REQUIRE(st.frames_ok == 100);
    REQUIRE(st.bytes_dropped_sync == 0);
}

TEST_CASE("justfloat garbage prefix before valid frames counts dropped only for garbage",
    "[protocol][justfloat]")
{
    // R-78 口径：帧头之前的垃圾字节计入 bytes_dropped_sync；帧负载不计入。
    JustFloatParser p(4);
    std::vector<std::byte> stream = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
        std::byte{0x04}, std::byte{0x05}, std::byte{0x06}, std::byte{0x07}};   // 7 B 垃圾
    auto f1 = frame({1.0F, 2.0F, 3.0F, 4.0F});
    auto f2 = frame({5.0F, 6.0F, 7.0F, 8.0F});
    stream.insert(stream.end(), f1.begin(), f1.end());
    stream.insert(stream.end(), f2.begin(), f2.end());

    p.feed(stream);
    const auto frames = drain(p);
    REQUIRE(frames.size() == 2);
    const auto st = p.stats();
    REQUIRE(st.frames_ok == 2);
    REQUIRE(st.bytes_dropped_sync == 7);     // 仅帧前垃圾
}

TEST_CASE("justfloat mixed garbage and batch frames drops only garbage",
    "[protocol][justfloat]")
{
    // R-75 验收补充：垃圾流与完整帧混合——只丢垃圾，完整帧全部产出。
    // 注意：垃圾中不能出现 00 00 80 7F 序列（否则构成候选帧尾）。
    JustFloatParser p(4);
    std::vector<std::byte> stream;
    for (int k = 0; k < 50; ++k) {
        stream.push_back(static_cast<std::byte>((k * 7 + 1) % 0xFF));  // 恒非 0x00
    }
    for (int k = 0; k < 3; ++k) {
        auto f = frame({static_cast<float>(k), 1.0F, 2.0F, 3.0F});
        stream.insert(stream.end(), f.begin(), f.end());
    }
    for (int k = 0; k < 50; ++k) {
        stream.push_back(static_cast<std::byte>((k * 7 + 1) % 0xFF));
    }

    p.feed(stream);
    const auto frames = drain(p);
    REQUIRE(frames.size() == 3);
    REQUIRE_THAT(frames[0].ch[0], WithinRel(0.0F));
    REQUIRE_THAT(frames[2].ch[0], WithinRel(2.0F));
    const auto st = p.stats();
    REQUIRE(st.frames_ok == 3);
    REQUIRE(st.bytes_dropped_sync == 50);    // 仅帧前垃圾；帧后垃圾留在缓冲等后续数据（≤cap 不丢）
}
