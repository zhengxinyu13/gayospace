// CLI 参数模型单测（第 29 轮评审 §2 批次①，测试向量先行；R-104 参数全集 + 退出码 2 边界）。
// 契约：docs/TS-SW.md §6（capture 三选一子命令 / send / list-ports；--duration|--samples 二选一；
// --out 必选；--duration 上限 3600s；退出码 2 = 参数错误）。
// 取舍注记（供评审对账）：--duration 0 视为非法（≤0 无意义，防呆，同 --samples ≤0）；
// 重复选项取最后一次（常规 CLI 语义，TS 未规定，不校验）；--script 文件存在性属 capture
// 运行期（批次②），参数解析不查文件系统。
#include <catch2/catch_test_macros.hpp>

#include "cli/CliOptions.h"

using namespace gayospace;
using Code = cli::CliExitCode;

namespace {

// 期望成功：errors 为空、exitCode 不被置位
#define REQUIRE_PARSE_OK(varname)                   \
    do {                                            \
        QStringList macro_errs;                     \
        auto macro_exit = Code::ArgError;           \
        auto varname = cli::CliOptions::parse(args, macro_errs, macro_exit); \
        REQUIRE(macro_errs.isEmpty());              \
        REQUIRE(macro_exit == Code::Ok);            \
        static_cast<void>(varname);                 \
    } while (false)

// 期望失败：exitCode==ArgError 且 errors 非空
#define REQUIRE_PARSE_ARG_ERROR()                   \
    do {                                            \
        QStringList macro_errs;                     \
        auto macro_exit = Code::Ok;                 \
        auto macro_opts = cli::CliOptions::parse(args, macro_errs, macro_exit); \
        REQUIRE(macro_exit == Code::ArgError);      \
        REQUIRE(!macro_errs.isEmpty());             \
        static_cast<void>(macro_opts);              \
    } while (false)

} // namespace

TEST_CASE("capture full argument set parses", "[cli][options]")
{
    QStringList args{"capture", "--port", "COM3", "--baud", "115200", "--protocol", "justfloat",
        "--channels", "8", "--rate", "1000", "--duration", "5", "--out", "data.json"};
    REQUIRE_PARSE_OK(opts);

    QStringList errs;
    auto exit = Code::ArgError;
    const auto o = cli::CliOptions::parse(args, errs, exit);
    REQUIRE(o.command == cli::CliCommand::Capture);
    REQUIRE(o.port == "COM3");
    REQUIRE(o.baud == 115200);
    REQUIRE(o.protocol == "justfloat");
    REQUIRE(o.channels == 8);
    REQUIRE(o.hasRate);
    REQUIRE(o.rateFs == 1000.0);
    REQUIRE(o.hasDuration);
    REQUIRE(o.durationSec == 5.0);
    REQUIRE(!o.hasSamples);
    REQUIRE(o.outPath == "data.json");
}

TEST_CASE("capture duration samples are mutually exclusive and mandatory", "[cli][options]")
{
    // R-104：二选一——两个都给 → 参数错误
    QStringList args{"capture", "--port", "COM3", "--baud", "115200", "--protocol", "justfloat",
        "--channels", "8", "--duration", "5", "--samples", "100", "--out", "data.json"};
    REQUIRE_PARSE_ARG_ERROR();

    // 都缺 → 参数错误
    args = QStringList{"capture", "--port", "COM3", "--baud", "115200", "--protocol", "justfloat",
        "--channels", "8", "--out", "data.json"};
    REQUIRE_PARSE_ARG_ERROR();
}

TEST_CASE("capture out is mandatory", "[cli][options]")
{
    // --out 缺失（评审 #9：stdout 只放进度，结构化数据只落文件）
    QStringList args{"capture", "--port", "COM3", "--baud", "115200", "--protocol", "justfloat",
        "--channels", "8", "--duration", "5"};
    REQUIRE_PARSE_ARG_ERROR();
}

TEST_CASE("capture duration bounds", "[cli][options]")
{
    // 上限 3600s（TS §6 防呆）；0 视为非法（取舍见文件头注记）
    QStringList args{"capture", "--port", "COM3", "--baud", "115200", "--protocol", "justfloat",
        "--channels", "8", "--duration", "3601", "--out", "data.json"};
    REQUIRE_PARSE_ARG_ERROR();

    args = QStringList{"capture", "--port", "COM3", "--baud", "115200", "--protocol", "justfloat",
        "--channels", "8", "--duration", "3600", "--out", "data.json"};
    REQUIRE_PARSE_OK(opts);

    args = QStringList{"capture", "--port", "COM3", "--baud", "115200", "--protocol", "justfloat",
        "--channels", "8", "--duration", "0", "--out", "data.json"};
    REQUIRE_PARSE_ARG_ERROR();

    args = QStringList{"capture", "--port", "COM3", "--baud", "115200", "--protocol", "justfloat",
        "--channels", "8", "--duration", "abc", "--out", "data.json"};
    REQUIRE_PARSE_ARG_ERROR();
}

TEST_CASE("capture samples bounds", "[cli][options]")
{
    QStringList args{"capture", "--port", "COM3", "--baud", "115200", "--protocol", "justfloat",
        "--channels", "8", "--samples", "0", "--out", "data.json"};
    REQUIRE_PARSE_ARG_ERROR();

    args = QStringList{"capture", "--port", "COM3", "--baud", "115200", "--protocol", "justfloat",
        "--channels", "8", "--samples", "-5", "--out", "data.json"};
    REQUIRE_PARSE_ARG_ERROR();

    args = QStringList{"capture", "--port", "COM3", "--baud", "115200", "--protocol", "justfloat",
        "--channels", "8", "--samples", "xyz", "--out", "data.json"};
    REQUIRE_PARSE_ARG_ERROR();

    args = QStringList{"capture", "--port", "COM3", "--baud", "115200", "--protocol", "justfloat",
        "--channels", "8", "--samples", "1", "--out", "data.json"};
    REQUIRE_PARSE_OK(opts);
}

TEST_CASE("capture channels bounds", "[cli][options]")
{
    // [1, 32] = protocol::kMaxChannels（与 JustFloatParser R-76 同口径）
    QStringList args{"capture", "--port", "COM3", "--baud", "115200", "--protocol", "justfloat",
        "--channels", "0", "--duration", "5", "--out", "data.json"};
    REQUIRE_PARSE_ARG_ERROR();

    args = QStringList{"capture", "--port", "COM3", "--baud", "115200", "--protocol", "justfloat",
        "--channels", "33", "--duration", "5", "--out", "data.json"};
    REQUIRE_PARSE_ARG_ERROR();

    args = QStringList{"capture", "--port", "COM3", "--baud", "115200", "--protocol", "justfloat",
        "--channels", "abc", "--duration", "5", "--out", "data.json"};
    REQUIRE_PARSE_ARG_ERROR();

    args = QStringList{"capture", "--port", "COM3", "--baud", "115200", "--protocol", "justfloat",
        "--channels", "32", "--duration", "5", "--out", "data.json"};
    REQUIRE_PARSE_OK(opts);
}

TEST_CASE("capture protocol validation", "[cli][options]")
{
    QStringList args{"capture", "--port", "COM3", "--baud", "115200", "--protocol", "modbus",
        "--channels", "8", "--duration", "5", "--out", "data.json"};
    REQUIRE_PARSE_ARG_ERROR();

    // script 协议：--script 可选（文件存在性属运行期，批次②）
    args = QStringList{"capture", "--port", "COM3", "--baud", "115200", "--protocol", "script",
        "--script", "conv.js", "--channels", "8", "--duration", "5", "--out", "data.json"};
    REQUIRE_PARSE_OK(opts);
}

TEST_CASE("capture rate validation", "[cli][options]")
{
    QStringList args{"capture", "--port", "COM3", "--baud", "115200", "--protocol", "justfloat",
        "--channels", "8", "--rate", "0", "--duration", "5", "--out", "data.json"};
    REQUIRE_PARSE_ARG_ERROR();

    // 缺省：hasRate=false → 输出字段名 t_index（TS §6 / R-106）
    args = QStringList{"capture", "--port", "COM3", "--baud", "115200", "--protocol", "justfloat",
        "--channels", "8", "--duration", "5", "--out", "data.json"};
    REQUIRE_PARSE_OK(opts);
    QStringList errs;
    auto exit = Code::ArgError;
    const auto o = cli::CliOptions::parse(args, errs, exit);
    REQUIRE(!o.hasRate);
}

TEST_CASE("send minimal and validation", "[cli][options]")
{
    QStringList args{"send", "--port", "COM3", "--baud", "115200", "--data", "01 02 03"};
    REQUIRE_PARSE_OK(opts);

    QStringList errs;
    auto exit = Code::ArgError;
    const auto o = cli::CliOptions::parse(args, errs, exit);
    REQUIRE(o.command == cli::CliCommand::Send);
    REQUIRE(o.data == "01 02 03");
    REQUIRE(!o.hex);
    REQUIRE(o.repeat == 1);
    REQUIRE(o.intervalMs == 0);

    args = QStringList{"send", "--port", "COM3", "--baud", "115200"};
    REQUIRE_PARSE_ARG_ERROR(); // --data 缺失

    args = QStringList{"send", "--port", "COM3", "--baud", "115200", "--data", "01", "--repeat", "0"};
    REQUIRE_PARSE_ARG_ERROR(); // --repeat ≤0

    args = QStringList{"send", "--port", "COM3", "--baud", "115200", "--data", "01", "--interval", "-1"};
    REQUIRE_PARSE_ARG_ERROR(); // --interval <0
}

TEST_CASE("send repeat upper bound is 100000", "[cli][options]")
{
    // R-129：契约上界 100000（TS §6.1 第 3 条实现约束，批次③收紧解析器谓词）
    QStringList args{"send", "--port", "COM3", "--data", "01", "--repeat", "100001"};
    REQUIRE_PARSE_ARG_ERROR(); // 越界上界 → 退出码 2

    args = QStringList{"send", "--port", "COM3", "--data", "01", "--repeat", "100000"};
    REQUIRE_PARSE_OK(opts); // 上界本身放行

    QStringList errs;
    auto exit = Code::ArgError;
    const auto o = cli::CliOptions::parse(args, errs, exit);
    REQUIRE(o.repeat == 100000);
}

TEST_CASE("port and baud belong to serial params validation surface", "[cli][options]")
{
    // R-104：--port/--baud 组装 SerialParams 后走 serial 层校验，失败 → 退出码 2
    QStringList args{"send", "--baud", "115200", "--data", "01"}; // --port 缺失
    REQUIRE_PARSE_ARG_ERROR();

    args = QStringList{"send", "--port", "COM3", "--baud", "abc", "--data", "01"};
    REQUIRE_PARSE_ARG_ERROR();

    args = QStringList{"send", "--port", "COM3", "--baud", "0", "--data", "01"};
    REQUIRE_PARSE_ARG_ERROR();
}

TEST_CASE("unknown subcommand and unknown option are argument errors", "[cli][options]")
{
    QStringList args{"frobnicate"};
    REQUIRE_PARSE_ARG_ERROR();

    args = QStringList{"capture", "--frobnicate", "1", "--port", "COM3", "--baud", "115200",
        "--protocol", "justfloat", "--channels", "8", "--duration", "5", "--out", "data.json"};
    REQUIRE_PARSE_ARG_ERROR();

    args = QStringList{}; // 空参数
    REQUIRE_PARSE_ARG_ERROR();
}

TEST_CASE("list-ports parses without options", "[cli][options]")
{
    QStringList args{"list-ports"};
    REQUIRE_PARSE_OK(opts);

    QStringList errs;
    auto exit = Code::ArgError;
    const auto o = cli::CliOptions::parse(args, errs, exit);
    REQUIRE(o.command == cli::CliCommand::ListPorts);
}
