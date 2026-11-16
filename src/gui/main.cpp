// GayoSpace GUI 入口骨架。
// Glassmorphism 视觉规范与 Qwt 波形页随 spike 结论落地（docs/TS-SW.md 第 5 节）。

#include <QApplication>
#include <QMainWindow>

#include "applogic/Version.h"
#include "gayospace_version.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("GayoSpace");
    QApplication::setApplicationVersion(QString::fromLatin1(gayospace::kVersion));

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("GayoSpace %1").arg(gayospace::versionString()));
    window.resize(1280, 800);
    window.show();
    return app.exec();
}

// This software was written by Grayson Zheng.
