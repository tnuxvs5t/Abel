#include "abelcore/abel_version.h"

#include <QtTest/QtTest>

class AbelSmokeTests final : public QObject {
    Q_OBJECT

private slots:
    void versionIsAvailable()
    {
        QVERIFY(abel::versionString().startsWith(QStringLiteral("Abel v0")));
        QVERIFY(abel::toolchainString().contains(QStringLiteral("Qt")));
    }
};

QTEST_MAIN(AbelSmokeTests)

#include "test_smoke.moc"

