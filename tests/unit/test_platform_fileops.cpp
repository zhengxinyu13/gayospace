// platform::FileOps 单测（R-103/R-13：原子替换语义；跨平台行为一致——Windows MoveFileExW
// MOVEFILE_REPLACE_EXISTING，POSIX rename 原子覆盖）。
// 核心回归向量：目标已存在时覆盖必须成功（QFile::rename 在 Windows 上会失败——R-13 修过的坑）。
#include <catch2/catch_test_macros.hpp>

#include <QString>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "platform/FileOps.h"

using gayospace::platform::replaceFile;

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir()
    {
        path = std::filesystem::temp_directory_path()
               / ("gayospace_test_" + std::to_string(std::chrono::steady_clock::now()
                                                         .time_since_epoch()
                                                         .count()));
        std::filesystem::create_directories(path);
    }
    ~TempDir() { std::filesystem::remove_all(path); }

    std::filesystem::path file(const char* name) const { return path / name; }

    static void writeFile(const std::filesystem::path& p, const std::string& content)
    {
        std::ofstream f(p, std::ios::binary);
        f << content;
    }
    static std::string readFile(const std::filesystem::path& p)
    {
        std::ifstream f(p, std::ios::binary);
        return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    }
};

} // namespace

TEST_CASE("replaceFile moves temp onto missing target", "[platform][fileops]")
{
    TempDir d;
    const auto from = d.file("out.json.tmp");
    const auto to = d.file("out.json");
    TempDir::writeFile(from, "payload");

    QString err;
    REQUIRE(replaceFile(from, to, err));
    REQUIRE(err.isEmpty());
    REQUIRE(TempDir::readFile(to) == "payload");
    REQUIRE_FALSE(std::filesystem::exists(from)); // 临时文件已消费
}

TEST_CASE("replaceFile overwrites existing target", "[platform][fileops]")
{
    // R-103 核心向量：重复执行 --out data.csv，目标已存在 → 覆盖成功
    TempDir d;
    const auto from = d.file("data.csv.tmp");
    const auto to = d.file("data.csv");
    TempDir::writeFile(to, "OLD CONTENT");
    TempDir::writeFile(from, "NEW CONTENT");

    QString err;
    REQUIRE(replaceFile(from, to, err));
    REQUIRE(TempDir::readFile(to) == "NEW CONTENT");
}

TEST_CASE("replaceFile fails when source missing", "[platform][fileops]")
{
    TempDir d;
    const auto from = d.file("ghost.tmp");
    const auto to = d.file("out.json");

    QString err;
    REQUIRE_FALSE(replaceFile(from, to, err));
    REQUIRE_FALSE(err.isEmpty()); // 用户可读错误信息
}
