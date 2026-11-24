// gayospace-cli 入口（批次②：capture 主链接线；TS-SW §6 契约）。
//
// 退出码映射（R-105）：0 正常完成 / 2 参数错误 / 3 串口打不开或断开重连失败 /
// 4 脚本异常（script 引擎未实现，后续批次）/ 5 落盘失败 / 130 Ctrl+C（数据先落盘）。
//
// 单线程事件循环（第 32 轮评审拍板）：SerialChannel + PipelinePump + CaptureSession
// 全部住主线程，QTimer(10ms) 轮询 tick——不引入 SerialWorker/PipelineThread（它们是
// GUI 多线程模式的封装，CLI 无并发需求，直连反而少一层信号转发）。
// parse_errors/frames_bad 不降退出码（R-105：垃圾流是数据质量问题）。

#include <iostream>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QObject>
#include <QSerialPortInfo>
#include <QTimer>

#include <atomic>

#include "applogic/PipelinePump.h"
#include "applogic/Version.h"
#include "cli/CaptureSession.h"
#include "cli/CaptureWriter.h"
#include "cli/CliOptions.h"
#include "gayospace_version.h"
#include "pipeline/SpscRingBuffer.h"
#include "platform/Interrupt.h"
#include "protocol/FireWaterParser.h"
#include "protocol/IProtocolParser.h"
#include "protocol/JustFloatParser.h"
#include "serial/QSerialTransport.h"
#include "serial/SerialChannel.h"

namespace {
// TS-SW §3.2：ring 默认 4 MB；§3.3：store 存储时长默认 600 s @1 kHz
constexpr std::size_t kRingCapacityBytes = 4U * 1024U * 1024U;
constexpr std::uint64_t kStoreCapacityFrames = 600000; // 600 s × 1 kHz

std::atomic<bool> g_interrupted { false };

void onInterruptSignal()
{
    // async-signal-safe：仅原子写（契约见 platform/Interrupt.h）
    g_interrupted.store(true, std::memory_order_relaxed);
}

std::unique_ptr<gayospace::protocol::IProtocolParser> makeParser(const gayospace::cli::CliOptions& opts)
{
    if (opts.protocol == QStringLiteral("firewater")) {
        return std::make_unique<gayospace::protocol::FireWaterParser>();
    }
    return std::make_unique<gayospace::protocol::JustFloatParser>(
        static_cast<std::uint8_t>(opts.channels));
}

int runCapture(QCoreApplication& app, const gayospace::cli::CliOptions& opts)
{
    // --protocol script：脚本引擎属后续批次（PR 说明），到这一步参数面已通过，显式拒绝
    if (opts.protocol == QStringLiteral("script")) {
        std::cerr << "capture --protocol script: engine not implemented yet (planned in a later batch)\n";
        return static_cast<int>(gayospace::cli::CliExitCode::ArgError);
    }

    gayospace::pipeline::SpscRingBuffer ring(kRingCapacityBytes);
    gayospace::applogic::PipelinePump pump(ring, makeParser(opts),
        static_cast<std::uint8_t>(opts.channels), kStoreCapacityFrames);

    gayospace::cli::CaptureMeta meta;
    meta.port = opts.port;
    meta.baud = opts.baud;
    meta.protocol = opts.protocol;
    meta.channels = opts.channels;
    meta.hasRate = opts.hasRate;
    meta.rateFs = opts.rateFs;
    gayospace::cli::CaptureWriter writer(meta, opts.outPath);

    QString err;
    if (!writer.begin(err)) {
        std::cerr << "capture: " << err.toStdString() << "\n";
        return static_cast<int>(gayospace::cli::CliExitCode::IoError);
    }

    // 串口会话（单线程直连；重连决策在 SerialChannel，错误只上报）
    gayospace::serial::QSerialTransport transport;
    gayospace::serial::SerialChannel channel(&transport);
    QObject::connect(&channel, &gayospace::serial::SerialChannel::bytesReceived,
        [&ring, hexEcho = opts.hexEcho](const QByteArray& data) {
            if (hexEcho) {
                std::cout << "rx " << data.toHex(' ').toStdString() << "\n";
            }
            ring.write(reinterpret_cast<const std::byte*>(data.constData()),
                static_cast<std::size_t>(data.size()));
        });

    // 初始打开失败 → 退出码 3（不自动重连，SerialChannel 契约）
    QString openError;
    QObject::connect(&channel, &gayospace::serial::SerialChannel::openFailed,
        [&openError](gayospace::serial::SerialErrorKind, const QString& message) {
            openError = message;
        });

    // 运行期断开：自动重连排程（reconnectScheduled → 延时触发 fireReconnectTimer）
    QObject::connect(&channel, &gayospace::serial::SerialChannel::reconnectScheduled,
        [&channel](int, int delayMs) {
            QTimer::singleShot(delayMs, &channel, &gayospace::serial::SerialChannel::fireReconnectTimer);
        });

    // 重连耗尽（Failed 终态）→ 数据先落盘，退出码 3
    std::atomic<bool> serialFailed { false };
    QObject::connect(&channel, &gayospace::serial::SerialChannel::stateChanged,
        [&serialFailed](gayospace::serial::SerialChannelState state) {
            if (state == gayospace::serial::SerialChannelState::Failed) {
                serialFailed.store(true, std::memory_order_relaxed);
            }
        });

    channel.open(opts.toSerialParams());
    if (channel.state() != gayospace::serial::SerialChannelState::Connected) {
        std::cerr << "capture: cannot open port " << opts.port.toStdString();
        if (!openError.isEmpty()) {
            std::cerr << " (" << openError.toStdString() << ")";
        }
        std::cerr << "\n";
        return static_cast<int>(gayospace::cli::CliExitCode::SerialError);
    }

    // Ctrl+C → 原子标志（async-signal-safe），tick 里转 requestStop；安装失败仅告警降级
    // （会话仍有 duration/samples 兜底退出路径）
    if (!gayospace::platform::installInterruptHandler(&onInterruptSignal)) {
        std::cerr << "capture: warning: Ctrl+C handler unavailable, use --duration to end session\n";
    }

    gayospace::cli::CaptureLimits limits;
    limits.hasDuration = opts.hasDuration;
    limits.durationMs = static_cast<qint64>(opts.durationSec * 1000.0);
    limits.hasSamples = opts.hasSamples;
    limits.samples = opts.samples;
    gayospace::cli::CaptureSession session(pump, writer, limits);

    QElapsedTimer elapsed;
    elapsed.start();

    int exitCode = static_cast<int>(gayospace::cli::CliExitCode::Ok);
    QString exitError;
    QTimer tickTimer;
    QObject::connect(&tickTimer, &QTimer::timeout, [&]() {
        if (g_interrupted.load(std::memory_order_relaxed)) {
            session.requestStop();
        }
        gayospace::cli::CliExitCode code = gayospace::cli::CliExitCode::Ok;
        QString tickErr;
        if (!session.tick(elapsed.elapsed(), code, tickErr)) {
            exitCode = static_cast<int>(code);
            exitError = tickErr;
            tickTimer.stop();
            QCoreApplication::quit(); // 静态槽，无需持有 app 引用
        }
    });
    tickTimer.start(10);

    app.exec();

    if (serialFailed.load(std::memory_order_relaxed) && exitCode == 0) {
        // 会话已把已收数据落盘（requestStop 语义）；退出码按 R-105 降为串口错误
        exitCode = static_cast<int>(gayospace::cli::CliExitCode::SerialError);
    }
    if (!exitError.isEmpty()) {
        std::cerr << "capture: " << exitError.toStdString() << "\n";
    }
    return exitCode;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("gayospace-cli");
    QCoreApplication::setApplicationVersion(QString::fromLatin1(gayospace::kVersion));

    const QStringList args = app.arguments().mid(1);
    if (args.contains(QStringLiteral("--version"))) {
        std::cout << "gayospace-cli " << gayospace::versionString().toStdString() << "\n";
        return 0;
    }

    QStringList errors;
    gayospace::cli::CliExitCode exit = gayospace::cli::CliExitCode::Ok;
    const auto opts = gayospace::cli::CliOptions::parse(args, errors, exit);
    if (!errors.isEmpty()) {
        for (const auto& e : errors) {
            std::cerr << "gayospace-cli: " << e.toStdString() << "\n";
        }
        return static_cast<int>(gayospace::cli::CliExitCode::ArgError);
    }

    switch (opts.command) {
    case gayospace::cli::CliCommand::ListPorts:
        for (const auto& info : QSerialPortInfo::availablePorts()) {
            std::cout << info.portName().toStdString()
                      << "\t" << info.description().toStdString() << "\n";
        }
        return 0;
    case gayospace::cli::CliCommand::Send:
        // 批次③实现（R-102：契约内 --data/--hex/--repeat/--interval）
        std::cerr << "send: not implemented yet (planned in a later batch)\n";
        return static_cast<int>(gayospace::cli::CliExitCode::ArgError);
    case gayospace::cli::CliCommand::Capture:
        return runCapture(app, opts);
    }
    return static_cast<int>(gayospace::cli::CliExitCode::ArgError);
}
