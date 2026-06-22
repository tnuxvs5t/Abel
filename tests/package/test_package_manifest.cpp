#include "abelcore/package_manifest.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

    void resolvesLocalPathDependenciesAndWritesLockfile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QDir root(dir.path());
        QVERIFY(root.mkpath(QStringLiteral("dep/src")));
        QVERIFY(root.mkpath(QStringLiteral("app/src")));
        writeText(root.absoluteFilePath(QStringLiteral("dep/src/main.abel")),
                  QStringLiteral("fn int main() { return 0; }"));
        writeText(root.absoluteFilePath(QStringLiteral("dep/abel.package.json")),
                  QStringLiteral(R"({
                      "name": "dep",
                      "version": "0.2.0",
                      "entry": "src/main.abel"
                  })"));
        writeText(root.absoluteFilePath(QStringLiteral("app/src/main.abel")),
                  QStringLiteral("fn int main() { return 0; }"));
        writeText(root.absoluteFilePath(QStringLiteral("app/abel.package.json")),
                  QStringLiteral(R"({
                      "name": "app",
                      "version": "0.1.0",
                      "entry": "src/main.abel",
                      "dependencies": [
                          {"name": "dep", "kind": "path", "path": "../dep"}
                      ]
                  })"));

        auto lock = abel::updatePackageLock(root.absoluteFilePath(QStringLiteral("app")));
        for (const auto& d : lock.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(lock.diagnostics.isEmpty());
        QCOMPARE(lock.rootName, QStringLiteral("app"));
        QCOMPARE(lock.entries.size(), 1);
        QCOMPARE(lock.entries.front().name, QStringLiteral("dep"));
        QCOMPARE(lock.entries.front().version, QStringLiteral("0.2.0"));
        QCOMPARE(lock.entries.front().kind, QStringLiteral("path"));
        QVERIFY(QFileInfo(lock.lockFile).isFile());

        QFile file(lock.lockFile);
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        QVERIFY(doc.isObject());
        QCOMPARE(doc.object().value(QStringLiteral("root")).toObject().value(QStringLiteral("name")).toString(),
                 QStringLiteral("app"));
        const QJsonArray packages = doc.object().value(QStringLiteral("packages")).toArray();
        QCOMPARE(packages.size(), 1);
        const QJsonObject lockedDep = packages.at(0).toObject();
        QCOMPARE(lockedDep.value(QStringLiteral("name")).toString(), QStringLiteral("dep"));
        QCOMPARE(lockedDep.value(QStringLiteral("kind")).toString(), QStringLiteral("path"));
        QVERIFY(QFileInfo(lockedDep.value(QStringLiteral("resolvedPath")).toString()).isDir());
    }

    void rejectsMissingPathDependency()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QDir root(dir.path());
        QVERIFY(root.mkpath(QStringLiteral("app/src")));
        writeText(root.absoluteFilePath(QStringLiteral("app/src/main.abel")),
                  QStringLiteral("fn int main() { return 0; }"));
        writeText(root.absoluteFilePath(QStringLiteral("app/abel.package.json")),
                  QStringLiteral(R"({
                      "name": "app",
                      "version": "0.1.0",
                      "entry": "src/main.abel",
                      "dependencies": [
                          {"name": "missing", "kind": "path", "path": "../missing"}
                      ]
                  })"));

        auto lock = abel::resolvePackageLock(root.absoluteFilePath(QStringLiteral("app")));
        QVERIFY(!lock.diagnostics.isEmpty());
        QVERIFY(!QFileInfo(QDir(root.absoluteFilePath(QStringLiteral("app"))).absoluteFilePath(abel::packageLockFileName())).exists());
    }
};

QTEST_MAIN(AbelPackageManifestTests)

#include "test_package_manifest.moc"
