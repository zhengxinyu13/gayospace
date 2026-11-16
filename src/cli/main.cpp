// gayospace-cli 入口骨架。
// 完整 capture/send/list-ports 契约见 docs/TS-SW.md 第 6 节，随业务编码逐步落地。

#include <iostream>

#include <QCoreApplication>

#include "applogic/Version.h"

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("gayospace-cli");
    QCoreApplication::setApplicationVersion(QString::fromLatin1(gayospace::kVersion));

    const QStringList args = app.arguments();
    if (args.contains(QStringLiteral("--version"))) {
        std::cout << "gayospace-cli " << gayospace::versionString().toStdString() << "\n";
        return 0;
    }

    std::cout << "usage: gayospace-cli [--version]\n"
              << "  capture / send / list-ports: implemented in later milestones (see docs/TS-SW.md section 6)\n";
    return 2;  // 退出码契约：参数错误（TS-SW 第 6 节）
}
