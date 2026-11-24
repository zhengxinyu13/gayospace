// 原子替换文件（R-13/R-103；TS-SW §11 平台收敛层，符号表已登记 MoveFileEx）。
// 语义：from 覆盖到 to（to 可存在）；成功后 from 消失。跨平台行为一致：
//  - Windows：MoveFileExW(MOVEFILE_REPLACE_EXISTING)——QFile::rename 在目标已存在时
//    返回 false（R-13 修过的坑），必须走 MoveFileEx；
//  - POSIX：rename() 本身原子覆盖。
// 非目标：跨卷移动、目录替换（capture 落盘场景 from/to 同目录）。
#pragma once

#include <QString>

#include <filesystem>

namespace gayospace::platform {

// 成功返回 true 且 error 清空；失败返回 false 且 error 为用户可读信息。
[[nodiscard]] bool replaceFile(const std::filesystem::path& from,
    const std::filesystem::path& to, QString& error);

} // namespace gayospace::platform
