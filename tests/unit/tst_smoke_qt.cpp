// Qt Test 冒烟用例：验证 Qt Test 接入与离屏运行（CI QT_QPA_PLATFORM=offscreen）。

#include <QtTest/QtTest>

#include "applogic/Version.h"

class SmokeQtTest : public QObject
{
    Q_OBJECT

private slots:
    void versionStringIsNonEmpty();
    void qVersionIsAtLeast68();
};

void SmokeQtTest::versionStringIsNonEmpty()
{
    QVERIFY(!gayospace::versionString().isEmpty());
}

void SmokeQtTest::qVersionIsAtLeast68()
{
    // TS-SW 第 1 节：Qt 6.8 LTS 基线
    QVERIFY(QT_VERSION_MAJOR > 6 || (QT_VERSION_MAJOR == 6 && QT_VERSION_MINOR >= 8));
}

QTEST_GUILESS_MAIN(SmokeQtTest)
#include "tst_smoke_qt.moc"
