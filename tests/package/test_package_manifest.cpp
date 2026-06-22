#include "abelcore/package_manifest.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

class AbelPackageManifestTests final : public QObject {
    Q_OBJECT

private:
    static void writeText(const QString& path, const QString& text)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QCOMPARE(file.write(text.toUtf8()), static_cast<qint64>(text.toUtf8().size()));
    }

private slots:
    void parsesProjectEntryAndBackendArtifacts()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("src")));
        writeText(QDir(dir.path()).absoluteFilePath(QStringLiteral("src/main.abel")),
                  QStringLiteral("fn int main() { return 0; }"));

        const QString manifest = QStringLiteral(R"({
            "name": "demo",
            "version": "0.1.0",
            "entry": "src/main.abel",
            "backendArtifacts": [
                {
                    "backendId": "MathSystem",
                    "path": "build/plugins/libmath_backend.so",
                    "symbols": ["fast_add", "MathSystem.sort"]
                }
            ]
        })");
        writeText(QDir(dir.path()).absoluteFilePath(abel::packageManifestFileName()), manifest);

        auto parsed = abel::packageManifestFromDirectory(dir.path());
        for (const auto& d : parsed.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(parsed.diagnostics.isEmpty());
        QCOMPARE(parsed.package.name, QStringLiteral("demo"));
        QCOMPARE(parsed.package.version, QStringLiteral("0.1.0"));
        QCOMPARE(parsed.package.entryFilePath(), QDir(dir.path()).absoluteFilePath(QStringLiteral("src/main.abel")));
        QCOMPARE(parsed.package.backendArtifacts.size(), 1);
        QCOMPARE(parsed.package.backendArtifacts.front().backendId, QStringLiteral("MathSystem"));
        QCOMPARE(parsed.package.backendArtifacts.front().symbols.size(), 2);
        QCOMPARE(parsed.package.backendArtifacts.front().symbols[0], QStringLiteral("MathSystem.fast_add"));
        QCOMPARE(parsed.package.backendArtifacts.front().symbols[1], QStringLiteral("MathSystem.sort"));
    }

    void rejectsMissingEntryFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeText(QDir(dir.path()).absoluteFilePath(abel::packageManifestFileName()),
                  QStringLiteral(R"({
                      "name": "broken",
                      "version": "0.1.0",
                      "entry": "src/missing.abel"
                  })"));

        auto parsed = abel::packageManifestFromDirectory(dir.path());
        QVERIFY(!parsed.diagnostics.isEmpty());
    }

    void identifiesPackageDirectory()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(!abel::isPackageDirectory(dir.path()));
        writeText(QDir(dir.path()).absoluteFilePath(abel::packageManifestFileName()),
                  QStringLiteral(R"({
                      "name": "demo",
                      "version": "0.1.0",
                      "entry": "main.abel"
                  })"));
        QVERIFY(abel::isPackageDirectory(dir.path()));
    }

    void initializesMinimalProject()
    {
        QTemporaryDir parent;
        QVERIFY(parent.isValid());
        const QString root = QDir(parent.path()).absoluteFilePath(QStringLiteral("hello_abel"));

        abel::PackageInitOptions options;
        options.rootDir = root;
        options.name = QStringLiteral("hello-abel");
        auto initialized = abel::initPackageProject(options);
        for (const auto& d : initialized.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(initialized.diagnostics.isEmpty());
        QCOMPARE(initialized.createdFiles.size(), 4);
        QVERIFY(QFileInfo(QDir(root).absoluteFilePath(abel::packageManifestFileName())).isFile());
        QVERIFY(QFileInfo(QDir(root).absoluteFilePath(QStringLiteral("src/main.abel"))).isFile());
        QVERIFY(QFileInfo(QDir(root).absoluteFilePath(QStringLiteral("README.md"))).isFile());
        QVERIFY(QFileInfo(QDir(root).absoluteFilePath(QStringLiteral(".gitignore"))).isFile());

        auto parsed = abel::packageManifestFromDirectory(root);
        for (const auto& d : parsed.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(parsed.diagnostics.isEmpty());
        QCOMPARE(parsed.package.name, QStringLiteral("hello-abel"));
        QCOMPARE(parsed.package.entry, QStringLiteral("src/main.abel"));
    }

    void initRefusesToOverwriteExistingFiles()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeText(QDir(dir.path()).absoluteFilePath(abel::packageManifestFileName()),
                  QStringLiteral("{}"));

        abel::PackageInitOptions options;
        options.rootDir = dir.path();
        auto initialized = abel::initPackageProject(options);
        QVERIFY(!initialized.diagnostics.isEmpty());
    }
};

QTEST_MAIN(AbelPackageManifestTests)

#include "test_package_manifest.moc"
