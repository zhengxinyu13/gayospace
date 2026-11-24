// CaptureSession 单测（批次②第二步；测试向量先行）。
// 覆盖：pump→writer 增量桥、samples/duration 停止判定、Ctrl+C 中断落盘（130 语义
// R-105：数据先落盘）、parse_errors 不降退出码（R-105）、落盘失败退出码 5（IoError）。
// 驱动方式：ring.write 注入字节 + 显式 tick(nowMs) 步进（单线程事件循环口径，无真串口）。
#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <filesystem>
#include <optional>
#include <string>

#include "applogic/PipelinePump.h"
#include "cli/CaptureSession.h"
#include "cli/CaptureWriter.h"
#include "protocol/FireWaterParser.h"
#include "protocol/IProtocolParser.h"
#include "protocol/JustFloatParser.h"

using namespace gayospace;

namespace {

// 协议 → parser 工厂（PipelinePump 构造即接管所有权；供成员初始化列表使用）
std::unique_ptr<protocol::IProtocolParser> makeParser(bool firewater)
{
    if (firewater) {
        return std::make_unique<protocol::FireWaterParser>();
    }
    return std::make_unique<protocol::JustFloatParser>(2);
}

struct SessionFixture {
    pipeline::SpscRingBuffer ring{64 * 1024};
    applogic::PipelinePump pump;
    std::filesystem::path dir;
    QString outPath;
    std::unique_ptr<cli::CaptureWriter> writer;
    bool firewater = false;

    explicit SessionFixture(bool okOut = true, bool useFirewater = false)
        : pump(ring, makeParser(useFirewater), 2, 4096),
          firewater(useFirewater)
    {
        dir = std::filesystem::temp_directory_path()
              / ("gayospace_session_"
                  + std::to_string(
                      std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(dir);
        if (okOut) {
            const auto p2 = dir / "out.json";
            outPath = QString::fromStdU16String(p2.u16string());
        } else {
            // IoError 向量：outPath 是已存在的目录——begin 写 <dir>.tmp 成功，
            // finish 原子替换到目录必失败（MoveFileEx/rename 均不可替换目录）
            outPath = QString::fromStdU16String(dir.u16string());
        }
        writer = std::make_unique<cli::CaptureWriter>(meta(), outPath);
    }
    ~SessionFixture() { std::filesystem::remove_all(dir); }

    cli::CaptureMeta meta() const
    {
        cli::CaptureMeta m;
        m.port = "COM3";
        m.baud = 115200;
        m.protocol = firewater ? "firewater" : "justfloat";
        m.channels = 2;
        m.hasRate = true;
        m.rateFs = 1000.0;
        return m;
    }

    // 单帧字节：justfloat = 2×float 小端 + 帧尾 00 00 80 7F（12B）；firewater = "a,b\n"
    QByteArray frame(float a, float b) const
    {
        if (firewater) {
            return QByteArray::number(static_cast<double>(a)) + ","
                   + QByteArray::number(static_cast<double>(b)) + "\n";
        }
        QByteArray f;
        const float vals[2] = {a, b};
        f.append(reinterpret_cast<const char*>(vals), sizeof(vals));
        const unsigned char tail[4] = {0x00, 0x00, 0x80, 0x7F};
        f.append(reinterpret_cast<const char*>(tail), 4);
        return f;
    }

    void inject(const QByteArray& bytes)
    {
        fxInject(bytes);
    }

    void fxInject(const QByteArray& bytes)
    {
        ring.write(reinterpret_cast<const std::byte*>(bytes.constData()),
            static_cast<std::size_t>(bytes.size()));
    }

    QJsonArray samples() const
    {
        QFile f(outPath);
        if (!f.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QJsonDocument::fromJson(f.readAll()).object()["samples"].toArray();
    }
};

cli::CaptureLimits limits(std::optional<qint64> samples, std::optional<qint64> durationMs)
{
    cli::CaptureLimits l;
    if (samples) {
        l.hasSamples = true;
        l.samples = *samples;
    }
    if (durationMs) {
        l.hasDuration = true;
        l.durationMs = *durationMs;
    }
    return l;
}

} // namespace

TEST_CASE("capture session bridges frames from ring to writer", "[cli][session]")
{
    SessionFixture fx;
    QString err;
    REQUIRE(fx.writer->begin(err));
    cli::CaptureSession session(fx.pump, *fx.writer, limits(std::nullopt, std::nullopt));

    fx.inject(fx.frame(1.0F, 2.0F));
    fx.inject(fx.frame(3.0F, 4.0F));

    cli::CliExitCode code = cli::CliExitCode::Ok;
    REQUIRE(session.tick(0, code, err)); // 无停止条件 → 继续运行
    REQUIRE(code == cli::CliExitCode::Ok);
    REQUIRE(session.writtenFrames() == 2);

    // 数据只在临时文件，需显式终结落盘后才能读 out.json 断言
    session.requestStop();
    REQUIRE_FALSE(session.tick(1, code, err));
    REQUIRE(code == cli::CliExitCode::Interrupted);

    const auto s = fx.samples();
    REQUIRE(s.size() == 2);
    REQUIRE(s[0].toObject()["t_ms"] == 0);
    REQUIRE(s[1].toObject()["t_ms"] == 1);
    REQUIRE(s[1].toObject()["ch"].toArray()[0] == 3.0);
}

TEST_CASE("capture session stops when samples limit reached", "[cli][session]")
{
    SessionFixture fx;
    QString err;
    REQUIRE(fx.writer->begin(err));
    cli::CaptureSession session(fx.pump, *fx.writer, limits(2, std::nullopt));

    for (int i = 0; i < 3; ++i) { // 写 3 帧，超过上限 2
        fx.inject(fx.frame(static_cast<float>(i), 0.0F));
    }

    cli::CliExitCode code = cli::CliExitCode::Ok;
    REQUIRE_FALSE(session.tick(0, code, err)); // 达到上限 → 会话终结
    REQUIRE(code == cli::CliExitCode::Ok);     // 正常完成 = 退出码 0（R-105）
    REQUIRE(session.writtenFrames() == 2);     // 只写上限数，第 3 帧不落盘
    REQUIRE(fx.samples().size() == 2);
}

TEST_CASE("capture session stops when duration elapsed", "[cli][session]")
{
    SessionFixture fx;
    QString err;
    REQUIRE(fx.writer->begin(err));
    cli::CaptureSession session(fx.pump, *fx.writer, limits(std::nullopt, 100));

    fx.inject(fx.frame(1.0F, 2.0F));

    cli::CliExitCode code = cli::CliExitCode::Ok;
    REQUIRE(session.tick(50, code, err));   // 未到期 → 继续
    REQUIRE(code == cli::CliExitCode::Ok);
    REQUIRE_FALSE(session.tick(100, code, err)); // nowMs >= durationMs → 终结
    REQUIRE(code == cli::CliExitCode::Ok);
    REQUIRE_FALSE(session.tick(101, code, err)); // 幂等：结果不变
    REQUIRE(code == cli::CliExitCode::Ok);
    REQUIRE(fx.samples().size() == 1);
}

TEST_CASE("capture session interrupt flushes data first", "[cli][session]")
{
    // Ctrl+C（130 语义）：已收数据先落盘再退出
    SessionFixture fx;
    QString err;
    REQUIRE(fx.writer->begin(err));
    cli::CaptureSession session(fx.pump, *fx.writer, limits(std::nullopt, std::nullopt));

    fx.inject(fx.frame(7.0F, 8.0F));
    session.requestStop();

    cli::CliExitCode code = cli::CliExitCode::Ok;
    REQUIRE_FALSE(session.tick(0, code, err)); // 下一个 tick 即终结
    REQUIRE(code == cli::CliExitCode::Interrupted);
    REQUIRE(fx.samples().size() == 1); // 数据已落盘
    REQUIRE(fx.samples()[0].toObject()["ch"].toArray()[0] == 7.0);
}

TEST_CASE("capture session parse errors do not lower exit code", "[cli][session]")
{
    // R-105：垃圾行（parse_errors>0）不降退出码——正常完成仍为 0。
    // 用 firewater 驱动：非法字段 strtof 失败 → parse_errors++（JustFloat 的无帧尾
    // 垃圾只是缓冲待同步，不进 parse_errors 计数）。
    SessionFixture fx(true, true);
    QString err;
    REQUIRE(fx.writer->begin(err));
    cli::CaptureSession session(fx.pump, *fx.writer, limits(1, std::nullopt));

    fx.inject("bad:notnum\n");  // 前缀剔除后字段解析失败 → parse_errors++
    fx.inject(fx.frame(1.0F, 2.0F)); // 好帧

    cli::CliExitCode code = cli::CliExitCode::Ok;
    REQUIRE_FALSE(session.tick(0, code, err));
    REQUIRE(code == cli::CliExitCode::Ok);
    REQUIRE(fx.pump.stats().parseErrors > 0); // 确实产生了解析错误
    REQUIRE(fx.samples().size() == 1);        // 好帧照常落盘
}

TEST_CASE("capture session maps io failure to IoError", "[cli][session]")
{
    // R-105：落盘失败 → 退出码 5
    SessionFixture fx(false); // outPath = 目录，finish 原子替换必失败
    QString err;
    REQUIRE(fx.writer->begin(err)); // 临时文件 <dir>.tmp 可写
    cli::CaptureSession session(fx.pump, *fx.writer, limits(1, std::nullopt));

    fx.inject(fx.frame(1.0F, 2.0F));

    cli::CliExitCode code = cli::CliExitCode::Ok;
    REQUIRE_FALSE(session.tick(0, code, err));
    REQUIRE(code == cli::CliExitCode::IoError);
    REQUIRE_FALSE(err.isEmpty());
}

TEST_CASE("capture session ticks incrementally without duplicates", "[cli][session]")
{
    // 多次 tick：帧序号推进无重复无遗漏（增量读 [lastWritten, frameIndex)）
    SessionFixture fx;
    QString err;
    REQUIRE(fx.writer->begin(err));
    cli::CaptureSession session(fx.pump, *fx.writer, limits(std::nullopt, std::nullopt));

    cli::CliExitCode code = cli::CliExitCode::Ok;
    for (int i = 0; i < 5; ++i) {
        fx.inject(fx.frame(static_cast<float>(i), 0.0F));
        REQUIRE(session.tick(i * 10, code, err));
        REQUIRE(code == cli::CliExitCode::Ok);
    }
    REQUIRE(session.writtenFrames() == 5);

    // 显式终结落盘后断言（数据在临时文件，finish 才替换出 out.json）
    session.requestStop();
    REQUIRE_FALSE(session.tick(50, code, err));
    REQUIRE(code == cli::CliExitCode::Interrupted);

    const auto s = fx.samples();
    REQUIRE(s.size() == 5);
    for (int i = 0; i < 5; ++i) {
        REQUIRE(s[i].toObject()["t_ms"] == i);
        REQUIRE(s[i].toObject()["ch"].toArray()[0] == static_cast<double>(i));
    }
}
