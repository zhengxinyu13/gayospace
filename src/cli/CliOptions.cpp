// CLI 参数模型实现（语义见头文件与 tests/cli/test_cli_options.cpp 用例注释）。
#include "cli/CliOptions.h"

#include <charconv>
#include <utility>

namespace gayospace::cli {

namespace {

// 数值解析（qint32 / int / qint64 / double 统一入口）：非数字 / 越界 → false
template <typename T>
bool toNumber(const QString& text, T& out)
{
    const auto u8 = text.toUtf8();
    const char* first = reinterpret_cast<const char*>(u8.constData());
    const char* last = first + u8.size();
    const auto [ptr, ec] = std::from_chars(first, last, out);
    return ec == std::errc{} && ptr == last; // 全串消费才合法（"12x" 拒绝）
}

void addError(QStringList& errors, const QString& message)
{
    errors.append(message);
}

// 消费 "--flag value"。value 缺失时记错误并返回 false（调用方不再重复报错）。
bool takeValue(const QStringList& args, int& i, const QString& flag, QString& out, QStringList& errors)
{
    if (i + 1 >= args.size()) {
        addError(errors, QStringLiteral("选项 %1 缺少参数值").arg(flag));
        return false;
    }
    out = args.at(++i);
    return true;
}

// "解析数值 + 约束校验" 组合：失败路径（缺值/非法值）已记错误，返回 false。
template <typename T, typename Pred>
bool parseNumber(const QStringList& args, int& i, const QString& flag, T& out,
    const Pred& valid, const QString& invalidMessage, QStringList& errors)
{
    QString value;
    if (!takeValue(args, i, flag, value, errors)) {
        return false;
    }
    if (!toNumber(value, out) || !valid(out)) {
        addError(errors, invalidMessage.arg(value));
        return false;
    }
    return true;
}

// 必选选项兜底检查
void requireNonEmpty(QStringList& errors, const QString& flag, const QString& value)
{
    if (value.isEmpty()) {
        addError(errors, QStringLiteral("缺少必选选项 %1").arg(flag));
    }
}

} // namespace

serial::SerialParams CliOptions::toSerialParams() const
{
    serial::SerialParams p;
    p.portName = port;
    p.baudRate = baud;
    return p;
}

CliOptions CliOptions::parse(const QStringList& args, QStringList& errors, CliExitCode& exitCode)
{
    exitCode = CliExitCode::Ok;
    CliOptions o;

    if (args.isEmpty()) {
        addError(errors, QStringLiteral("缺少子命令：capture | send | list-ports"));
        exitCode = CliExitCode::ArgError;
        return o;
    }

    const QString sub = args.first();
    if (sub == QStringLiteral("list-ports")) {
        if (args.size() > 1) {
            addError(errors, QStringLiteral("list-ports 不接受任何选项"));
            exitCode = CliExitCode::ArgError;
        }
        o.command = CliCommand::ListPorts;
        return o;
    }
    if (sub != QStringLiteral("capture") && sub != QStringLiteral("send")) {
        addError(errors, QStringLiteral("未知子命令 %1（可用：capture | send | list-ports）").arg(sub));
        exitCode = CliExitCode::ArgError;
        return o;
    }
    const bool isCapture = sub == QStringLiteral("capture");
    o.command = isCapture ? CliCommand::Capture : CliCommand::Send;

    for (int i = 1; i < args.size(); ++i) {
        const QString flag = args.at(i);
        QString value;

        if (flag == QStringLiteral("--port")) {
            if (takeValue(args, i, flag, value, errors)) {
                o.port = value;
            }
        } else if (flag == QStringLiteral("--baud")) {
            qint32 baud = 0;
            if (parseNumber(args, i, flag, baud, [](qint32 v) { return v > 0; },
                    QStringLiteral("--baud 的值须为正整数：%1"), errors)) {
                o.baud = baud;
            }
        } else if (isCapture && flag == QStringLiteral("--protocol")) {
            if (takeValue(args, i, flag, value, errors)) {
                o.protocol = value;
            }
        } else if (isCapture && flag == QStringLiteral("--script")) {
            if (takeValue(args, i, flag, value, errors)) {
                o.scriptFile = value; // 文件存在性属 capture 运行期（批次②）
            }
        } else if (isCapture && flag == QStringLiteral("--channels")) {
            int ch = 0;
            if (parseNumber(args, i, flag, ch, [](int v) { return v >= 1 && v <= 32; }, // kMaxChannels
                    QStringLiteral("--channels 的值须为 1~32：%1"), errors)) {
                o.channels = ch;
            }
        } else if (isCapture && flag == QStringLiteral("--rate")) {
            double fs = 0;
            if (parseNumber(args, i, flag, fs, [](double v) { return v > 0; },
                    QStringLiteral("--rate 的值须为正数：%1"), errors)) {
                o.hasRate = true;
                o.rateFs = fs;
            }
        } else if (isCapture && flag == QStringLiteral("--duration")) {
            double sec = 0;
            if (parseNumber(args, i, flag, sec, [](double v) { return v > 0; },
                    QStringLiteral("--duration 的值须为正数：%1"), errors)) {
                o.hasDuration = true;
                o.durationSec = sec;
            }
        } else if (isCapture && flag == QStringLiteral("--samples")) {
            qint64 n = 0;
            if (parseNumber(args, i, flag, n, [](qint64 v) { return v > 0; },
                    QStringLiteral("--samples 的值须为正整数：%1"), errors)) {
                o.hasSamples = true;
                o.samples = n;
            }
        } else if (isCapture && flag == QStringLiteral("--out")) {
            if (takeValue(args, i, flag, value, errors)) {
                o.outPath = value;
            }
        } else if (isCapture && flag == QStringLiteral("--hex-echo")) {
            o.hexEcho = true;
        } else if (!isCapture && flag == QStringLiteral("--data")) {
            if (takeValue(args, i, flag, value, errors)) {
                o.data = value;
            }
        } else if (!isCapture && flag == QStringLiteral("--hex")) {
            o.hex = true;
        } else if (!isCapture && flag == QStringLiteral("--repeat")) {
            int n = 0;
            if (parseNumber(args, i, flag, n, [](int v) { return v >= 1; },
                    QStringLiteral("--repeat 的值须为正整数：%1"), errors)) {
                o.repeat = n;
            }
        } else if (!isCapture && flag == QStringLiteral("--interval")) {
            qint64 ms = 0;
            if (parseNumber(args, i, flag, ms, [](qint64 v) { return v >= 0; },
                    QStringLiteral("--interval 的值须为非负整数：%1"), errors)) {
                o.intervalMs = ms;
            }
        } else {
            addError(errors, QStringLiteral("未知选项 %1").arg(flag));
        }
    }

    // ---- 共用校验（R-104：--port/--baud 属 SerialParams 校验面）----
    requireNonEmpty(errors, QStringLiteral("--port"), o.port);
    const auto sp = o.toSerialParams();
    for (const QString& e : sp.validationErrors()) {
        addError(errors, e);
    }

    // ---- 子命令专属校验 ----
    if (isCapture) {
        requireNonEmpty(errors, QStringLiteral("--protocol"), o.protocol);
        if (!o.protocol.isEmpty() && o.protocol != QStringLiteral("justfloat")
            && o.protocol != QStringLiteral("firewater") && o.protocol != QStringLiteral("script")) {
            addError(errors, QStringLiteral("--protocol 须为 justfloat | firewater | script"));
        }
        requireNonEmpty(errors, QStringLiteral("--out"), o.outPath);
        if (o.channels < 1) {
            addError(errors, QStringLiteral("缺少必选选项 --channels（1~32）"));
        }
        if (!o.hasDuration && !o.hasSamples) {
            addError(errors, QStringLiteral("--duration 与 --samples 必须二选一"));
        } else if (o.hasDuration && o.hasSamples) {
            addError(errors, QStringLiteral("--duration 与 --samples 只能二选一"));
        } else if (o.hasDuration) {
            if (o.durationSec <= 0) {
                addError(errors, QStringLiteral("--duration 须为正数"));
            } else if (o.durationSec > 3600) {
                addError(errors, QStringLiteral("--duration 上限 3600 秒（TS §6 防呆）"));
            }
        } else if (o.samples <= 0) {
            addError(errors, QStringLiteral("--samples 须为正整数"));
        }
    } else { // Send
        requireNonEmpty(errors, QStringLiteral("--data"), o.data);
    }

    if (!errors.isEmpty()) {
        exitCode = CliExitCode::ArgError;
    }
    return o;
}

} // namespace gayospace::cli
