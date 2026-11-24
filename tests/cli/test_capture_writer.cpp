// CaptureWriter 单测（批次②，测试向量先行；TS-SW §6 JSON/CSV 流式写 + 原子落盘）。
// 覆盖：R-103 原子替换（目标已存在重跑覆盖）、R-106 t_index 改名（rate 缺省）、
// R-86 rx_bytes 组装、CSV NaN 空字段、JSON 合法性（QJsonDocument 解析验证）。
// 取舍注记（供评审对账）：JSON 对 NaN/Inf 输出 null（JSON 标准不允许 NaN 字面量，
// 与 CSV 空字段语义对齐——TS §6 未定义 JSON NaN 表示，评审可裁决）。
#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <filesystem>
#include <limits>

#include "applogic/PipelineStats.h"
#include "cli/CaptureWriter.h"

using namespace gayospace;

namespace {

struct WriterFixture {
    std::filesystem::path dir;
    cli::CaptureMeta meta;
    QString outPath;

    explicit WriterFixture(QString proto = "justfloat", bool withRate = true)
    {
        dir = std::filesystem::temp_directory_path()
              / ("gayospace_writer_"
                  + std::to_string(
                      std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(dir);
        const auto p = dir / "out.json";
        outPath = QString::fromStdU16String(p.u16string());
        meta.port = "COM3";
        meta.baud = 115200;
        meta.protocol = proto;
        meta.channels = 2;
        meta.hasRate = withRate;
        meta.rateFs = withRate ? 1000.0 : 0.0;
    }
    ~WriterFixture() { std::filesystem::remove_all(dir); }

    QString csvPath() const
    {
        const auto p = dir / "out.csv";
        return QString::fromStdU16String(p.u16string());
    }

    QJsonDocument readJson() const
    {
        QFile f(outPath);
        REQUIRE(f.open(QIODevice::ReadOnly));
        return QJsonDocument::fromJson(f.readAll());
    }
};

applogic::PipelineStats makeStats()
{
    applogic::PipelineStats s;
    s.ringBytes = 1000;
    s.rxDropped = 23; // R-86：rx_bytes = ringBytes + rxDropped = 1023
    s.droppedBytes = 0;
    s.parseErrors = 2;
    return s;
}

} // namespace

TEST_CASE("capture writer json with rate emits t_ms", "[cli][writer]")
{
    WriterFixture fx;
    cli::CaptureWriter w(fx.meta, fx.outPath);
    QString err;
    REQUIRE(w.begin(err));

    const float ch1[2] = {1.0F, 2.0F};
    const float ch2[2] = {1.1F, 2.0F};
    REQUIRE(w.writeFrame(0, ch1, 2, err));
    REQUIRE(w.writeFrame(1, ch2, 2, err));
    REQUIRE(w.finish(makeStats(), err));

    const auto doc = fx.readJson();
    REQUIRE(doc.isObject());
    const auto root = doc.object();

    const auto m = root["meta"].toObject();
    REQUIRE(m["tool"] == "gayospace");
    REQUIRE(m["port"] == "COM3");
    REQUIRE(m["baud"] == 115200);
    REQUIRE(m["protocol"] == "justfloat");
    REQUIRE(m["channels"] == 2);
    REQUIRE(m["rate"] == 1000);
    REQUIRE(m.contains("started_at"));

    // samples：t_ms = round(index * 1000 / rate)
    const auto samples = root["samples"].toArray();
    REQUIRE(samples.size() == 2);
    REQUIRE(samples[0].toObject()["t_ms"] == 0);
    REQUIRE(samples[1].toObject()["t_ms"] == 1);
    REQUIRE(samples[0].toObject()["ch"].toArray()[0] == 1.0);
    REQUIRE(samples[0].toObject()["ch"].toArray()[1] == 2.0);

    // stats：R-86 组装 rx_bytes = ringBytes + rxDropped
    const auto s = root["stats"].toObject();
    REQUIRE(s["rx_bytes"] == 1023);
    REQUIRE(s["dropped_bytes"] == 0);
    REQUIRE(s["rx_dropped"] == 23);
    REQUIRE(s["parse_errors"] == 2);
}

TEST_CASE("capture writer json without rate emits t_index", "[cli][writer]")
{
    // R-106：--rate 缺省 -> t_ms 改名 t_index（宁可显式缺省，不静默失真）
    WriterFixture fx("justfloat", false);
    cli::CaptureWriter w(fx.meta, fx.outPath);
    QString err;
    REQUIRE(w.begin(err));

    const float ch[2] = {1.0F, 2.0F};
    REQUIRE(w.writeFrame(7, ch, 2, err)); // 帧序号非 0，验证透传
    REQUIRE(w.finish(makeStats(), err));

    const auto root = fx.readJson().object();
    REQUIRE_FALSE(root["meta"].toObject().contains("rate")); // 缺省 -> meta 无 rate
    const auto samples = root["samples"].toArray();
    REQUIRE(samples.size() == 1);
    REQUIRE(samples[0].toObject().contains("t_index"));
    REQUIRE_FALSE(samples[0].toObject().contains("t_ms"));
    REQUIRE(samples[0].toObject()["t_index"] == 7);
}

TEST_CASE("capture writer json emits null for NaN channels", "[cli][writer]")
{
    // JSON 标准不允许 NaN 字面量 -> 输出 null（取舍见文件头注记）
    WriterFixture fx;
    cli::CaptureWriter w(fx.meta, fx.outPath);
    QString err;
    REQUIRE(w.begin(err));

    const float ch[2] = {std::numeric_limits<float>::quiet_NaN(), 3.0F};
    REQUIRE(w.writeFrame(0, ch, 2, err));
    REQUIRE(w.finish(makeStats(), err));

    const auto samples = fx.readJson().object()["samples"].toArray();
    REQUIRE(samples[0].toObject()["ch"].toArray()[0].isNull());
    REQUIRE(samples[0].toObject()["ch"].toArray()[1] == 3.0);
}

TEST_CASE("capture writer csv header and nan empty field", "[cli][writer]")
{
    WriterFixture fx("firewater", true);
    const QString csv = fx.csvPath();
    cli::CaptureWriter w(fx.meta, csv);
    QString err;
    REQUIRE(w.begin(err));

    const float ch[2] = {1.5F, std::numeric_limits<float>::quiet_NaN()};
    REQUIRE(w.writeFrame(0, ch, 2, err));
    REQUIRE(w.finish(makeStats(), err));

    QFile f(csv);
    REQUIRE(f.open(QIODevice::ReadOnly));
    const auto lines = f.readAll().split('\n');
    REQUIRE(lines.size() >= 3);
    REQUIRE(lines[0] == "t_ms,ch0,ch1"); // 首行（R-40 统一口径）
    REQUIRE(lines[1].startsWith("0,1.5,")); // NaN -> 空字段
    REQUIRE(lines[1].endsWith(","));
}

TEST_CASE("capture writer csv without rate uses t_index first column", "[cli][writer]")
{
    // R-106：CSV 首列与 JSON meta 同规则
    WriterFixture fx("firewater", false);
    const QString csv = fx.csvPath();
    cli::CaptureWriter w(fx.meta, csv);
    QString err;
    REQUIRE(w.begin(err));
    const float ch[2] = {1.0F, 2.0F};
    REQUIRE(w.writeFrame(3, ch, 2, err));
    REQUIRE(w.finish(makeStats(), err));

    QFile f(csv);
    REQUIRE(f.open(QIODevice::ReadOnly));
    const auto lines = f.readAll().split('\n');
    REQUIRE(lines[0] == "t_index,ch0,ch1");
    // 注：浮点按 'g' 9 位有效数字输出（1.0 → "1"），不做 %f 补零
    REQUIRE(lines[1].startsWith("3,1,"));
}

TEST_CASE("capture writer atomically overwrites existing target", "[cli][writer]")
{
    // R-103 核心向量：目标已存在 -> 重跑覆盖成功
    WriterFixture fx;
    {
        QFile old(fx.outPath);
        REQUIRE(old.open(QIODevice::WriteOnly));
        old.write("OLD");
    }
    {
        cli::CaptureWriter w(fx.meta, fx.outPath);
        QString err;
        REQUIRE(w.begin(err));
        const float ch[2] = {9.0F, 9.0F};
        REQUIRE(w.writeFrame(0, ch, 2, err));
        REQUIRE(w.finish(makeStats(), err));
        REQUIRE(w.finished());
    }
    REQUIRE(QFile::exists(fx.outPath));
    const auto doc = fx.readJson();
    REQUIRE(doc.object()["samples"].toArray().size() == 1); // 覆盖为本次内容
}

TEST_CASE("capture writer double finish is an error", "[cli][writer]")
{
    // R-105 退出码 5 的前置：finish 的状态机错误必须可见（error 非空 + 返回 false）
    WriterFixture fx;
    cli::CaptureWriter w(fx.meta, fx.outPath);
    QString err;
    REQUIRE(w.begin(err));
    REQUIRE(w.finish(makeStats(), err));
    QString err2;
    REQUIRE_FALSE(w.finish(makeStats(), err2)); // 二次 finish
    REQUIRE_FALSE(err2.isEmpty());
}
