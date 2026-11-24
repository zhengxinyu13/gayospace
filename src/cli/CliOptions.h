// CLI 参数模型（第 29 轮评审批次①；TS-SW §6 稳定契约的解析与校验面）。
// 纯解析：不碰事件循环、不碰串口、不碰文件系统（--script 文件存在性属 capture 运行期，
// 批次②）；错误逐条入 errors，任一错误 → exitCode = ArgError（退出码 2）。
//
// R-104（第 29 轮评审）：参数全集 = --port/--baud/--protocol/--channels/--rate/--out/
// --duration/--samples（capture 二选一）/ --data/--hex/--repeat/--interval（send）。
// R-102 拍板（2026-11-24）：第 29 轮切法稿中"脚本/文件发送"为笔误，send 范围收敛为
// 契约内 --data/--hex/--repeat/--interval，不加需求。
#pragma once

#include <QString>
#include <QStringList>

#include "serial/SerialParams.h"

namespace gayospace::cli {

// TS §6 退出码契约（六码；批次②③逐码落地，本批只用 Ok/ArgError）
enum class CliExitCode : int {
    Ok = 0,
    ArgError = 2,       // 参数错误
    SerialError = 3,    // 打不开 / 断开且重连失败 / 端口被占用
    ProtocolFatal = 4,  // 仅脚本未捕获异常 / 脚本超时
    IoError = 5,        // 落盘失败
    Interrupted = 130,  // Ctrl+C（已收数据先落盘）
};

enum class CliCommand { Capture, Send, ListPorts };

struct CliOptions {
    CliCommand command = CliCommand::ListPorts;

    // capture / send 共用（R-104：经 SerialParams 校验面）
    QString port;
    qint32 baud = 115200;

    // capture
    QString protocol;      // "justfloat" | "firewater" | "script"
    QString scriptFile;    // --script（可选；protocol=script 时的脚本路径）
    int channels = 0;      // [1, 32] = protocol::kMaxChannels
    bool hasRate = false;  // --rate 缺省 → 输出字段名 t_index（R-106）
    double rateFs = 0.0;
    bool hasDuration = false;
    double durationSec = 0.0; // 上限 3600s（防呆）
    bool hasSamples = false;
    qint64 samples = 0;
    QString outPath;       // capture 必选（评审 #9）
    bool hexEcho = false;  // --hex-echo

    // send（R-102 拍板：契约内 --data/--hex/--repeat/--interval，无脚本/文件发送）
    QString data;
    bool hex = false;
    int repeat = 1;
    qint64 intervalMs = 0;

    // 组装串口参数（port/baud 之外的 8N1 默认由 SerialParams 默认值给出）
    [[nodiscard]] serial::SerialParams toSerialParams() const;

    // 解析入口。成功：errors 空、exitCode == Ok；失败：errors 非空（用户可读逐条）、
    // exitCode == ArgError（options 内容未定义，调用方不得使用）。
    static CliOptions parse(const QStringList& args, QStringList& errors, CliExitCode& exitCode);
};

} // namespace gayospace::cli
