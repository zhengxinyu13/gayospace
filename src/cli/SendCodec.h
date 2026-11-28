// send 编码器（批次③第一刀；TS-SW §6.1 第 1/2 条 + R-128 校验先于开端口）。
// 纯函数：无 IO、无事件循环——CLI 在打开端口前调用（R-128：任一参数错误即退出码 2，
// 与端口是否可用无关）。失败时 bytes 内容未定义，调用方不得使用。
#pragma once

#include <QByteArray>
#include <QString>

namespace gayospace::cli {

struct SendCodecResult {
    QByteArray bytes;
    QString error; // 非空 = 词法非法（用户可读；退出码 2 由 CLI 调用方映射）
    bool ok() const { return error.isEmpty(); }
};

// §6.1 第 1 条：!hex 时 data 按字面 UTF-8 原样（含空白），不做隐式 hex 识别。
// §6.1 第 2 条：hex 时接受 [0-9A-Fa-f]，空白/逗号作分隔，"0x"/"0X" 前缀可带（仅
// token 开头），每字节须 2 位（奇数位/纯前缀/空内容拒绝）。
[[nodiscard]] SendCodecResult encodeSendPayload(const QString& data, bool hex);

} // namespace gayospace::cli
