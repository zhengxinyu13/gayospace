// 原子替换文件实现（契约见头文件）。
#include "platform/FileOps.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#endif

namespace gayospace::platform {

bool replaceFile(const std::filesystem::path& from, const std::filesystem::path& to, QString& error)
{
    error.clear();
#ifdef _WIN32
    const auto fromW = from.wstring();
    const auto toW = to.wstring();
    if (!MoveFileExW(fromW.c_str(), toW.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        const auto code = GetLastError();
        error = QStringLiteral("原子替换文件失败（MoveFileEx 错误码 %1）：%2 -> %3")
                    .arg(code)
                    .arg(QString::fromStdWString(fromW),
                        QString::fromStdWString(toW));
        return false;
    }
    return true;
#else
    if (::rename(from.c_str(), to.c_str()) != 0) {
        error = QStringLiteral("原子替换文件失败（errno %1：%2）：%3 -> %4")
                    .arg(errno)
                    .arg(QString::fromLocal8Bit(std::strerror(errno)))
                    .arg(QString::fromStdString(from.string()),
                        QString::fromStdString(to.string()));
        return false;
    }
    return true;
#endif
}

} // namespace gayospace::platform
