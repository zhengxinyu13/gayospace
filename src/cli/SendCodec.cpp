// send 编码器实现（语义见头文件与 tests/cli/test_send_codec.cpp 用例注释）。
#include "cli/SendCodec.h"

#include <QRegularExpression>

namespace gayospace::cli {

namespace {

bool isHexDigit(QChar c)
{
    return (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
        || (c >= QLatin1Char('a') && c <= QLatin1Char('f'))
        || (c >= QLatin1Char('A') && c <= QLatin1Char('F'));
}

int hexValue(QChar c)
{
    if (c <= QLatin1Char('9')) {
        return c.unicode() - QLatin1Char('0').unicode();
    }
    if (c >= QLatin1Char('a')) {
        return c.unicode() - QLatin1Char('a').unicode() + 10;
    }
    return c.unicode() - QLatin1Char('A').unicode() + 10;
}

} // namespace

SendCodecResult encodeSendPayload(const QString& data, bool hex)
{
    SendCodecResult r;
    if (!hex) {
        // §6.1 第 1 条：字面 UTF-8 原样，含空白，不做隐式 hex 识别（防静默错发）
        r.bytes = data.toUtf8();
        if (r.bytes.isEmpty()) {
            r.error = QStringLiteral("--data 为空");
        }
        return r;
    }

    // §6.1 第 2 条：空白/逗号作分隔；每 token 须偶数个 hex digit
    static const QRegularExpression separator{QStringLiteral("[\\s,]+")};
    const auto tokens = data.split(separator, Qt::SkipEmptyParts);
    if (tokens.isEmpty()) {
        r.error = QStringLiteral("--hex 内容为空（仅空白/逗号）");
        return r;
    }
    for (const QString& token : tokens) {
        QString body = token;
        if (body.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
            body.remove(0, 2);
        }
        for (const QChar c : body) {
            if (!isHexDigit(c)) {
                r.error = QStringLiteral("非法十六进制字符：%1").arg(token);
                return r;
            }
        }
        if (body.isEmpty() || body.size() % 2 != 0) {
            // §6.1 第 2 条：奇数位拒绝（"1" 补位或截断均有歧义）；纯前缀 token 同此报法
            r.error = QStringLiteral("每字节须 2 位：%1").arg(token);
            return r;
        }
        for (int i = 0; i < body.size(); i += 2) {
            r.bytes.append(static_cast<char>(hexValue(body.at(i)) * 16 + hexValue(body.at(i + 1))));
        }
    }
    return r;
}

} // namespace gayospace::cli
