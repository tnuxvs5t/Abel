#include "abelcore/abel_version.h"

#include <QtTest/QtTest>

class AbelSmokeTests final : public QObject {
    Q_OBJECT

private slots:
    void versionIsAvailable()
    {
        QCOMPARE(abel::versionString(), QStringLiteral("Abel v1.3"));
        QVERIFY(abel::toolchainString().contains(QStringLiteral("Qt")));
    }
};

QTEST_MAIN(AbelSmokeTests)

#include "test_smoke.moc"
