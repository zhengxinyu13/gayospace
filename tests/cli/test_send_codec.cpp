// send 编码器单测（批次③第一刀，向量先行；TS-SW §6.1 第 1/2/7 条 + R-128 校验先于开端口）。
// 契约：无 --hex 时字面 UTF-8 原样（不做隐式 hex 识别）；--hex 词法 = [0-9A-Fa-f]、
// 空白/逗号分隔、0x 前缀可带、每字节须 2 位（奇数位拒绝）、空串/只剩分隔符拒绝。
// SendCodec 为纯函数（无 IO/无事件循环），CLI 校验阶段调用，失败映射退出码 2。
#include <catch2/catch_test_macros.hpp>

#include "cli/SendCodec.h"

using gayospace::cli::encodeSendPayload;

namespace {
void requireOk(const QString& data, bool hex, const QByteArray& expected)
{
    const auto r = encodeSendPayload(data, hex);
    INFO("data=[" << data.toStdString() << "] error=[" << r.error.toStdString() << "]");
    REQUIRE(r.ok());
    REQUIRE(r.bytes == expected);
}

void requireError(const QString& data, bool hex, const QString& errorPart)
{
    const auto r = encodeSendPayload(data, hex);
    INFO("data=[" << data.toStdString() << "] error=[" << r.error.toStdString() << "]");
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error.contains(errorPart));
}
} // namespace

// ---- §6.1 第 1 条：字面 UTF-8 原样（无 --hex）----

TEST_CASE("literal ascii passes through byte for byte", "[cli][sendcodec]")
{
    // "01 02 03" 是 8 字节字面（防隐式 hex 识别的经典歧义）
    requireOk(QStringLiteral("01 02 03"), false, QByteArray("01 02 03"));
}

TEST_CASE("literal utf-8 multibyte passes through", "[cli][sendcodec]")
{
    // "你好" = 6 字节 UTF-8（E4 BD A0 E5 A5 BD）
    const QByteArray expected("\xE4\xBD\xA0\xE5\xA5\xBD", 6);
    requireOk(QStringLiteral("你好"), false, expected);
}

TEST_CASE("literal whitespace and control chars pass through", "[cli][sendcodec]")
{
    requireOk(QStringLiteral("ab\tc\nd "), false, QByteArray("ab\tc\nd "));
}

// ---- §6.1 第 2 条：--hex 词法 ----

TEST_CASE("hex space separated", "[cli][sendcodec]")
{
    requireOk(QStringLiteral("01 02 03"), true, QByteArray("\x01\x02\x03", 3));
}

TEST_CASE("hex comma separated", "[cli][sendcodec]")
{
    requireOk(QStringLiteral("01,02,03"), true, QByteArray("\x01\x02\x03", 3));
}

TEST_CASE("hex mixed separators", "[cli][sendcodec]")
{
    requireOk(QStringLiteral("0xff, 0x00\tAA"), true, QByteArray("\xFF\x00\xAA", 3));
}

TEST_CASE("hex 0x prefix optional and case insensitive", "[cli][sendcodec]")
{
    requireOk(QStringLiteral("0x01 0X02"), true, QByteArray("\x01\x02", 2));
}

TEST_CASE("hex digits case insensitive", "[cli][sendcodec]")
{
    requireOk(QStringLiteral("aB cD"), true, QByteArray("\xAB\xCD", 2));
}

TEST_CASE("hex contiguous digits without separator", "[cli][sendcodec]")
{
    requireOk(QStringLiteral("0102"), true, QByteArray("\x01\x02", 2));
}

TEST_CASE("hex 00 and 7f edge bytes", "[cli][sendcodec]")
{
    requireOk(QStringLiteral("00 7f"), true, QByteArray("\x00\x7F", 2));
}

// ---- 拒绝路径（退出码 2 由调用方映射；SendCodec 层只报错）----

TEST_CASE("hex odd digit count rejected", "[cli][sendcodec]")
{
    requireError(QStringLiteral("1 02"), true, QStringLiteral("每字节须 2 位"));
    requireError(QStringLiteral("0x1"), true, QStringLiteral("每字节须 2 位"));
    requireError(QStringLiteral("0102030"), true, QStringLiteral("每字节须 2 位"));
}

TEST_CASE("hex illegal character rejected", "[cli][sendcodec]")
{
    requireError(QStringLiteral("0G"), true, QStringLiteral("非法十六进制字符"));
    // 前缀只认 token 开头："010x02" 中段 x 非法
    requireError(QStringLiteral("010x02"), true, QStringLiteral("非法十六进制字符"));
}

TEST_CASE("hex empty or separator only rejected", "[cli][sendcodec]")
{
    requireError(QStringLiteral(""), true, QStringLiteral("--hex 内容为空"));
    requireError(QStringLiteral("  ,  "), true, QStringLiteral("--hex 内容为空"));
    // 纯前缀 token（"0x"）剥壳后为空 → 按每字节须 2 位报
    requireError(QStringLiteral("0x"), true, QStringLiteral("每字节须 2 位"));
}

TEST_CASE("literal empty string rejected defensively", "[cli][sendcodec]")
{
    // CLI 解析层 requireNonEmpty 先拦；编码器自身兜底防绕过
    requireError(QStringLiteral(""), false, QStringLiteral("--data 为空"));
}
