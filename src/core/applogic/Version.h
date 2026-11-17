#pragma once

#include <QString>

namespace gayospace {

// 版本信息（编译期注入，运行时可读出；AGENTS.md 9.1 软件等价物，无 SKU/HW Rev）
QString versionString();

}  // namespace gayospace
