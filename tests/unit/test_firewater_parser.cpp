// FireWater 解析器单测（TS-SW §4.2 + 第 17 轮评审 §2.3 测试向量清单）。
// 期望行为与实现取舍逐条写在各用例注释里，供评审对账。
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstddef>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "protocol/FireWaterParser.h"

using namespace gayospace::protocol;
using Catch::Matchers::WithinRel;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

std::vector<std::byte> toBytes(const std::string& s)
{
    std::vector<std::byte> out;
    out.reserve(s.size());
    for (const char c : s) {
        out.push_back(static_cast<std::byte>(c));
    }
    return out;
}

std::vector<ParseFrame> drain(IProtocolParser& p)
{
    std::vector<ParseFrame> out;
    while (auto f = p.pop_frame()) {
        out.push_back(*f);
    }
    return out;
}

} // namespace

TEST_CASE("firewater plain csv lines parse in order", "[protocol][firewater]")
{
    FireWaterParser p;
    p.feed(toBytes("1,2,3\n4,5,6\n"));
    const auto frames = drain(p);

    REQUIRE(frames.size() == 2);
    REQUIRE(frames[0].ch_count == 3);
    REQUIRE(frames[0].frame_index == 0);
    REQUIRE(frames[1].frame_index == 1);
    REQUIRE_THAT(frames[0].ch[0], WithinRel(1.0F));
    REQUIRE_THAT(frames[0].ch[2], WithinRel(3.0F));
    REQUIRE_THAT(frames[1].ch[1], WithinRel(5.0F));
    REQUIRE(p.stats().frames_ok == 2);
    REQUIRE(p.stats().parse_errors == 0);
}

TEST_CASE("firewater named prefixes are stripped", "[protocol][firewater]")
{
    // R-25/R-37 定稿：字母/下划线/含数字名称（ch1/pid1/v2/#1/channels）都归前缀剔除。
    // 首行 2 字段建立通道基准，各前缀行剔除后同为 2 字段（不触字段数门禁）。
    FireWaterParser p;
    p.feed(toBytes("1,2\nchannels:3,4\nch1:5,6\npid1:7,8\nv2:9,10\n#1:11,12\n"));
    const auto frames = drain(p);

    REQUIRE(frames.size() == 6);
    REQUIRE_THAT(frames[0].ch[0], WithinRel(1.0F));
    REQUIRE_THAT(frames[1].ch[0], WithinRel(3.0F));
    REQUIRE_THAT(frames[2].ch[0], WithinRel(5.0F));
    REQUIRE_THAT(frames[3].ch[0], WithinRel(7.0F));
    REQUIRE_THAT(frames[4].ch[0], WithinRel(9.0F));
    REQUIRE_THAT(frames[5].ch[1], WithinRel(12.0F));
    REQUIRE(p.stats().frames_bad == 0);
}

TEST_CASE("firewater channels prefix with three fields sets baseline", "[protocol][firewater]")
{
    // 评审反例向量 channels:1,2,3：前缀剔除后首行为基准（3 通道）
    FireWaterParser p;
    p.feed(toBytes("channels:1,2,3\n4,5,6\n"));
    const auto frames = drain(p);

    REQUIRE(frames.size() == 2);
    REQUIRE(frames[0].ch_count == 3);
    REQUIRE_THAT(frames[0].ch[2], WithinRel(3.0F));
    REQUIRE_THAT(frames[1].ch[1], WithinRel(5.0F));
}

TEST_CASE("firewater numeric prefix stays as data", "[protocol][firewater]")
{
    // "1.0:2.0,3.0"：前段是数据 → 不剔除，整行进宽容解析；
    // 字段 "1.0:2.0" strtof 宽容取出 1.0，ch0=1.0、ch1=3.0
    FireWaterParser p;
    p.feed(toBytes("1.0:2.0,3.0\n"));
    const auto frames = drain(p);

    REQUIRE(frames.size() == 1);
    REQUIRE(frames[0].ch_count == 2);
    REQUIRE_THAT(frames[0].ch[0], WithinRel(1.0F));
    REQUIRE_THAT(frames[0].ch[1], WithinRel(3.0F));
}

TEST_CASE("firewater image prefix drops whole line", "[protocol][firewater]")
{
    // 官方约定：any 为 image 的行整行丢弃并计数（TS §4.2）
    FireWaterParser p;
    p.feed(toBytes("image:1,2\n1,2\n"));
    const auto frames = drain(p);

    REQUIRE(frames.size() == 1);             // image 行丢弃，1,2 正常
    REQUIRE(p.stats().frames_bad == 1);
}

TEST_CASE("firewater empty prefix and no-colon lines parse leniently", "[protocol][firewater]")
{
    // 空前缀 ":1,2"（数据从冒号后）与无冒号 "1,2" 按数据宽容解析
    FireWaterParser p;
    p.feed(toBytes(":1,2\n3,4\n"));
    const auto frames = drain(p);

    REQUIRE(frames.size() == 2);
    REQUIRE_THAT(frames[0].ch[0], WithinRel(1.0F));
    REQUIRE_THAT(frames[0].ch[1], WithinRel(2.0F));
    REQUIRE_THAT(frames[1].ch[0], WithinRel(3.0F));
}

TEST_CASE("firewater blank line is ignored", "[protocol][firewater]")
{
    // 空行：静默忽略，不计错误（实现取舍，注释见 FireWaterParser.cpp handle_line）
    FireWaterParser p;
    p.feed(toBytes("\n1,2\n\n"));
    const auto frames = drain(p);
    REQUIRE(frames.size() == 1);
    const auto st = p.stats();
    REQUIRE(st.frames_bad == 0);
    REQUIRE(st.parse_errors == 0);
}

TEST_CASE("firewater handles crlf and lncr line endings", "[protocol][firewater]")
{
    // 兼容 \r\n 与 \n\r（TS §4.2）
    FireWaterParser p;
    p.feed(toBytes("1,2\r\n3,4\n\r5,6\n"));
    const auto frames = drain(p);

    REQUIRE(frames.size() == 3);
    REQUIRE_THAT(frames[0].ch[0], WithinRel(1.0F));
    REQUIRE_THAT(frames[1].ch[0], WithinRel(3.0F));
    REQUIRE_THAT(frames[2].ch[0], WithinRel(5.0F));
}

TEST_CASE("firewater partial line waits for newline", "[protocol][firewater]")
{
    // 半行截断：无 \n 不产出；分块到达后补全
    FireWaterParser p;
    p.feed(toBytes("1,"));
    REQUIRE(drain(p).empty());
    p.feed(toBytes("2\n"));
    const auto frames = drain(p);
    REQUIRE(frames.size() == 1);
    REQUIRE_THAT(frames[0].ch[1], WithinRel(2.0F));
}

TEST_CASE("firewater bad fields become nan and count parse_errors", "[protocol][firewater]")
{
    // 非法字段置 NaN 并 parse_errors++，不丢整行（TS §4.2）
    FireWaterParser p;
    p.feed(toBytes("1,abc,3\n"));
    const auto frames = drain(p);

    REQUIRE(frames.size() == 1);
    REQUIRE(std::isnan(frames[0].ch[1]));
    REQUIRE_THAT(frames[0].ch[0], WithinRel(1.0F));
    REQUIRE_THAT(frames[0].ch[2], WithinRel(3.0F));
    REQUIRE(p.stats().parse_errors == 1);
    REQUIRE(p.stats().frames_ok == 1);
    REQUIRE(p.stats().frames_bad == 0);
}

TEST_CASE("firewater field count change from first row counts bad", "[protocol][firewater]")
{
    // 通道数以首行为基准，后续行字段数变化计错误、不产出（TS §3.3）
    FireWaterParser p;
    p.feed(toBytes("1,2,3\n4,5\n6,7,8\n"));
    const auto frames = drain(p);

    REQUIRE(frames.size() == 2);             // 中间 2 字段行丢弃
    REQUIRE(frames[0].ch_count == 3);
    REQUIRE_THAT(frames[1].ch[0], WithinRel(6.0F));
    REQUIRE(p.stats().frames_bad == 1);
}

TEST_CASE("firewater byte-by-byte feed matches whole-block feed", "[protocol][firewater]")
{
    // 逐字节 feed 与整块 feed 结果一致（评审 §2.3）
    const std::string stream = "ch1:1,2,3\r\n4,5,6\nimage:9,9\n:7,8,x\n";

    FireWaterParser block;
    block.feed(toBytes(stream));
    const auto expected = drain(block);

    FireWaterParser byteByByte;
    for (const char c : stream) {
        const auto b = static_cast<std::byte>(c);
        byteByByte.feed(std::span<const std::byte>(&b, 1));
    }
    const auto actual = drain(byteByByte);

    REQUIRE(actual.size() == expected.size());
    REQUIRE(actual.size() == 3);             // 前缀行 + 数字行 + 丢 image + 坏字段行
    for (std::size_t i = 0; i < expected.size(); ++i) {
        REQUIRE(actual[i].frame_index == expected[i].frame_index);
        for (std::size_t c = 0; c < expected[i].ch_count; ++c) {
            // NaN != NaN：坏字段（NaN）按"双方皆 NaN"判等
            if (std::isnan(expected[i].ch[c])) {
                REQUIRE(std::isnan(actual[i].ch[c]));
            } else {
                REQUIRE(actual[i].ch[c] == expected[i].ch[c]);
            }
        }
    }
    const auto st = byteByByte.stats();
    REQUIRE(st.frames_bad == 1);             // image 行
    REQUIRE(st.parse_errors == 1);           // "x" 字段
}

TEST_CASE("firewater oversized line is dropped and counted", "[protocol][firewater]")
{
    // 行超长（> kMaxLineBytes）：整行丢弃并计数，防垃圾流无限增长（评审 §2.3）
    FireWaterParser p;
    std::string big;
    big.reserve(FireWaterParser::kMaxLineBytes + 64);
    for (std::size_t i = 0; i < FireWaterParser::kMaxLineBytes + 64; ++i) {
        big.push_back(static_cast<char>('0' + (i % 10)));
    }
    big.push_back('\n');
    p.feed(toBytes(big));

    REQUIRE(drain(p).empty());
    REQUIRE(p.stats().frames_bad == 1);

    // 超长行结束后恢复解析
    p.feed(toBytes("1,2\n"));
    const auto frames = drain(p);
    REQUIRE(frames.size() == 1);
    REQUIRE(p.stats().frames_bad == 1);      // 不重复计数
}

TEST_CASE("firewater prefix-only line is ignored without error", "[protocol][firewater]")
{
    // R-77："ch1:" 数据部分为空 → 忽略整行不计错，不产出 NaN 帧；
    // 前后正常行（2 字段）不受影响，通道基准不紊乱。
    FireWaterParser p;
    p.feed(toBytes("1,2\nch1:\n3,4\n"));
    const auto frames = drain(p);

    REQUIRE(frames.size() == 2);
    REQUIRE_THAT(frames[0].ch[0], WithinRel(1.0F));
    REQUIRE_THAT(frames[1].ch[0], WithinRel(3.0F));
    const auto st = p.stats();
    REQUIRE(st.frames_ok == 2);
    REQUIRE(st.frames_bad == 0);
    REQUIRE(st.parse_errors == 0);
}

TEST_CASE("firewater row with more than 32 fields is rejected", "[protocol][firewater]")
{
    // R-76：>kMaxChannels 字段的行必须整行丢弃并计错（下游按 ch_count 读 ch[32] 会越界），
    // base_ch_count_ 永不 > 32；后续正常行照常建立基准。
    FireWaterParser p;
    std::string row;
    for (int i = 1; i <= 33; ++i) {
        if (i > 1) {
            row.push_back(',');
        }
        row += std::to_string(i);
    }
    row.push_back('\n');
    p.feed(toBytes(row));                    // 33 字段

    REQUIRE(drain(p).empty());
    REQUIRE(p.stats().frames_bad == 1);

    p.feed(toBytes("1,2\n"));                // 正常行照常解析
    const auto frames = drain(p);
    REQUIRE(frames.size() == 1);
    REQUIRE(frames[0].ch_count == 2);
    REQUIRE(p.stats().frames_bad == 1);      // 不重复计数
}

TEST_CASE("firewater reset clears channel baseline and stats", "[protocol][firewater]")
{
    FireWaterParser p;
    p.feed(toBytes("1,2,3\n"));
    REQUIRE(drain(p).size() == 1);

    p.reset();
    const auto st = p.stats();
    REQUIRE(st.frames_ok == 0);
    REQUIRE(st.frames_bad == 0);
    REQUIRE(st.parse_errors == 0);
    REQUIRE_FALSE(p.pop_frame().has_value());

    // reset 后通道数基准重建：新流首行 2 字段也合法
    p.feed(toBytes("4,5\n"));
    const auto frames = drain(p);
    REQUIRE(frames.size() == 1);
    REQUIRE(frames[0].ch_count == 2);
    REQUIRE(frames[0].frame_index == 0);
}
