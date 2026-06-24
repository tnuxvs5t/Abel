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

    static void writePackage(QDir root, const QString& relativeRoot, const QString& name, const QString& version)
    {
        QVERIFY(root.mkpath(relativeRoot + QStringLiteral("/src")));
        writeText(root.absoluteFilePath(relativeRoot + QStringLiteral("/src/main.abel")),
                  QStringLiteral("fn int main() { return 0; }"));
        writeText(root.absoluteFilePath(relativeRoot + QStringLiteral("/abel.package.json")),
                  QStringLiteral(R"({
                      "name": "%1",
                      "version": "%2",
                      "entry": "src/main.abel"
                  })").arg(name, version));
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
        QCOMPARE(parsed.package.backendArtifacts.front().qtVersion, abel::currentAbelQtVersion());
        QCOMPARE(parsed.package.backendArtifacts.front().kit, abel::currentAbelQtKit());
        QCOMPARE(parsed.package.backendArtifacts.front().platform, abel::currentAbelPlatform());
        QCOMPARE(parsed.package.backendArtifacts.front().compiler, abel::currentAbelCompiler());
        QCOMPARE(parsed.package.backendArtifacts.front().compilerVersion, abel::currentAbelCompilerVersion());
        QCOMPARE(parsed.package.backendArtifacts.front().cxxStandard, abel::currentAbelCxxStandard());
        QCOMPARE(parsed.package.backendArtifacts.front().abelAbi, abel::currentAbelAbi());
    }

    void packageSourceFilesIncludeSrcTreeWithEntryLast()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir root(dir.path());
        QVERIFY(root.mkpath(QStringLiteral("src/lib")));
        writeText(root.absoluteFilePath(QStringLiteral("src/main.abel")),
                  QStringLiteral("module app.main; fn int main() { return helper(); }"));
        writeText(root.absoluteFilePath(QStringLiteral("src/lib/math.abel")),
                  QStringLiteral("module app.lib.math; fn int helper() { return 7; }"));
        writeText(root.absoluteFilePath(QStringLiteral("src/extra.abel")),
                  QStringLiteral("module app.extra; fn int extra() { return 1; }"));
        writeText(root.absoluteFilePath(abel::packageManifestFileName()),
                  QStringLiteral(R"({
                      "name": "demo",
                      "version": "0.1.0",
                      "entry": "src/main.abel"
                  })"));

        auto parsed = abel::packageManifestFromDirectory(dir.path());
        QVERIFY(parsed.diagnostics.isEmpty());
        const QStringList sources = abel::packageSourceFiles(parsed.package);
        QCOMPARE(sources.size(), 3);
        QCOMPARE(sources.back(), parsed.package.entryFilePath());
        QVERIFY(sources.contains(root.absoluteFilePath(QStringLiteral("src/lib/math.abel"))));
        QVERIFY(sources.contains(root.absoluteFilePath(QStringLiteral("src/extra.abel"))));
    }

    void packageGraphSourceFilesIncludeDependencyLibrariesButNotDependencyEntry()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir root(dir.path());
        QVERIFY(root.mkpath(QStringLiteral("dep/src/lib")));
        QVERIFY(root.mkpath(QStringLiteral("app/src")));
        writeText(root.absoluteFilePath(QStringLiteral("dep/src/main.abel")),
                  QStringLiteral("fn int main() { return 0; }"));
        writeText(root.absoluteFilePath(QStringLiteral("dep/src/lib/math.abel")),
                  QStringLiteral("export fn int dep_value() { return 41; }"));
        writeText(root.absoluteFilePath(QStringLiteral("dep/abel.package.json")),
                  QStringLiteral(R"({
                      "name": "dep",
                      "version": "0.1.0",
                      "entry": "src/main.abel"
                  })"));
        writeText(root.absoluteFilePath(QStringLiteral("app/src/main.abel")),
                  QStringLiteral("fn int main() { return dep_value() + 1; }"));
        writeText(root.absoluteFilePath(QStringLiteral("app/abel.package.json")),
                  QStringLiteral(R"({
                      "name": "app",
                      "version": "0.1.0",
                      "entry": "src/main.abel",
                      "dependencies": [
                          {"name": "dep", "kind": "path", "path": "../dep"}
                      ]
                  })"));

        auto graph = abel::packageGraphFromDirectory(root.absoluteFilePath(QStringLiteral("app")));
        for (const auto& d : graph.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(graph.diagnostics.isEmpty());

        const QStringList sources = abel::packageGraphSourceFiles(graph);
        QCOMPARE(sources.size(), 2);
        QVERIFY(sources.contains(root.absoluteFilePath(QStringLiteral("dep/src/lib/math.abel"))));
        QVERIFY(!sources.contains(root.absoluteFilePath(QStringLiteral("dep/src/main.abel"))));
        QCOMPARE(sources.back(), root.absoluteFilePath(QStringLiteral("app/src/main.abel")));
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

    void resolvesPathDependencyVersionRequirements()
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
                      "version": "0.2.3",
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
                          {"name": "dep", "kind": "path", "path": "../dep", "version": "^0.2.0"}
                      ]
                  })"));

        auto lock = abel::updatePackageLock(root.absoluteFilePath(QStringLiteral("app")));
        for (const auto& d : lock.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(lock.diagnostics.isEmpty());
        QCOMPARE(lock.entries.size(), 1);
        QCOMPARE(lock.entries.front().name, QStringLiteral("dep"));
        QCOMPARE(lock.entries.front().version, QStringLiteral("0.2.3"));
        QCOMPARE(lock.entries.front().versionRequirement, QStringLiteral("^0.2.0"));

        QFile file(lock.lockFile);
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        const QJsonObject lockedDep = doc.object().value(QStringLiteral("packages")).toArray().at(0).toObject();
        QCOMPARE(lockedDep.value(QStringLiteral("version")).toString(), QStringLiteral("0.2.3"));
        QCOMPARE(lockedDep.value(QStringLiteral("versionRequirement")).toString(), QStringLiteral("^0.2.0"));

        auto graph = abel::packageGraphFromDirectory(root.absoluteFilePath(QStringLiteral("app")));
        for (const auto& d : graph.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(graph.diagnostics.isEmpty());
        QCOMPARE(graph.dependencies.size(), 1);
        QCOMPARE(graph.dependencies.front().version, QStringLiteral("0.2.3"));
    }

    void rejectsUnsatisfiedPathDependencyVersion()
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
                      "version": "0.3.0",
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
                          {"name": "dep", "kind": "path", "path": "../dep", "version": "^0.2.0"}
                      ]
                  })"));

        auto lock = abel::resolvePackageLock(root.absoluteFilePath(QStringLiteral("app")));
        QVERIFY(!lock.diagnostics.isEmpty());
        bool sawVersion = false;
        for (const auto& d : lock.diagnostics)
            sawVersion = sawVersion || d.message.contains(QStringLiteral("version"));
        QVERIFY(sawVersion);
        QCOMPARE(lock.entries.size(), 0);
    }

    void resolvesRegistryDependencyAndCachesHighestSatisfyingVersion()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QDir root(dir.path());
        writePackage(root, QStringLiteral("registry/dep/1.0.0"), QStringLiteral("dep"), QStringLiteral("1.0.0"));
        writePackage(root, QStringLiteral("registry/dep/1.2.0"), QStringLiteral("dep"), QStringLiteral("1.2.0"));
        writePackage(root, QStringLiteral("registry/dep/2.0.0"), QStringLiteral("dep"), QStringLiteral("2.0.0"));
        writePackage(root, QStringLiteral("app"), QStringLiteral("app"), QStringLiteral("0.1.0"));
        writeText(root.absoluteFilePath(QStringLiteral("app/abel.package.json")),
                  QStringLiteral(R"({
                      "name": "app",
                      "version": "0.1.0",
                      "entry": "src/main.abel",
                      "dependencies": [
                          {"name": "dep", "kind": "registry", "registry": "../registry", "version": "^1.0.0"}
                      ]
                  })"));

        const QString appRoot = root.absoluteFilePath(QStringLiteral("app"));
        auto graph = abel::updatePackageGraph(appRoot);
        for (const auto& d : graph.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(graph.diagnostics.isEmpty());
        QCOMPARE(graph.entries.size(), 1);
        QCOMPARE(graph.entries.front().name, QStringLiteral("dep"));
        QCOMPARE(graph.entries.front().version, QStringLiteral("1.2.0"));
        QCOMPARE(graph.entries.front().versionRequirement, QStringLiteral("^1.0.0"));
        QCOMPARE(graph.entries.front().kind, QStringLiteral("registry"));
        QCOMPARE(graph.entries.front().source, QStringLiteral("../registry"));

        const QString cachedRoot = QDir(appRoot).absoluteFilePath(QStringLiteral(".abel/cache/packages/dep/1.2.0"));
        QCOMPARE(graph.entries.front().resolvedPath, cachedRoot);
        QVERIFY(QFileInfo(QDir(cachedRoot).absoluteFilePath(QStringLiteral("abel.package.json"))).isFile());
        QVERIFY(QFileInfo(QDir(cachedRoot).absoluteFilePath(QStringLiteral("src/main.abel"))).isFile());
        QVERIFY(!QFileInfo(QDir(appRoot).absoluteFilePath(QStringLiteral(".abel/cache/packages/dep/2.0.0"))).exists());
        QCOMPARE(graph.dependencies.size(), 1);
        QCOMPARE(graph.dependencies.front().name, QStringLiteral("dep"));
        QCOMPARE(graph.dependencies.front().version, QStringLiteral("1.2.0"));
        QCOMPARE(graph.dependencies.front().rootDir, cachedRoot);

        auto fromExistingLock = abel::packageGraphFromDirectory(appRoot);
        for (const auto& d : fromExistingLock.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(fromExistingLock.diagnostics.isEmpty());
        QCOMPARE(fromExistingLock.dependencies.size(), 1);
        QCOMPARE(fromExistingLock.dependencies.front().rootDir, cachedRoot);
    }

    void rejectsUnsatisfiedRegistryDependencyVersion()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QDir root(dir.path());
        writePackage(root, QStringLiteral("registry/dep/2.0.0"), QStringLiteral("dep"), QStringLiteral("2.0.0"));
        writePackage(root, QStringLiteral("app"), QStringLiteral("app"), QStringLiteral("0.1.0"));
        writeText(root.absoluteFilePath(QStringLiteral("app/abel.package.json")),
                  QStringLiteral(R"({
                      "name": "app",
                      "version": "0.1.0",
                      "entry": "src/main.abel",
                      "dependencies": [
                          {"name": "dep", "kind": "registry", "registry": "../registry", "version": "^1.0.0"}
                      ]
                  })"));

        auto lock = abel::resolvePackageLock(root.absoluteFilePath(QStringLiteral("app")));
        QVERIFY(!lock.diagnostics.isEmpty());
        bool sawRegistry = false;
        bool sawVersion = false;
        for (const auto& d : lock.diagnostics) {
            sawRegistry = sawRegistry || d.message.contains(QStringLiteral("registry"));
            sawVersion = sawVersion || d.message.contains(QStringLiteral("^1.0.0"));
        }
        QVERIFY(sawRegistry);
        QVERIFY(sawVersion);
        QCOMPARE(lock.entries.size(), 0);
    }

    void addsRegistryDependencyAndUpdatesLockfile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QDir root(dir.path());
        writePackage(root, QStringLiteral("registry/dep/1.0.0"), QStringLiteral("dep"), QStringLiteral("1.0.0"));
        writePackage(root, QStringLiteral("registry/dep/1.1.0"), QStringLiteral("dep"), QStringLiteral("1.1.0"));
        writePackage(root, QStringLiteral("app"), QStringLiteral("app"), QStringLiteral("0.1.0"));

        const QString appRoot = root.absoluteFilePath(QStringLiteral("app"));
        const QString registryRoot = root.absoluteFilePath(QStringLiteral("registry"));
        auto changed = abel::addRegistryPackageDependency(appRoot,
                                                          QStringLiteral("dep"),
                                                          QStringLiteral("^1.0.0"),
                                                          registryRoot);
        for (const auto& d : changed.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(changed.diagnostics.isEmpty());
        QVERIFY(changed.changed);
        QCOMPARE(changed.dependency.name, QStringLiteral("dep"));
        QCOMPARE(changed.dependency.kind, QStringLiteral("registry"));
        QCOMPARE(changed.dependency.registry, QStringLiteral("../registry"));
        QCOMPARE(changed.dependency.version, QStringLiteral("^1.0.0"));
        QCOMPARE(changed.lockedPackages, 1);
        QVERIFY(QFileInfo(changed.lockFile).isFile());

        auto parsed = abel::packageManifestFromDirectory(appRoot);
        for (const auto& d : parsed.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(parsed.diagnostics.isEmpty());
        QCOMPARE(parsed.package.dependencies.size(), 1);
        QCOMPARE(parsed.package.dependencies.front().kind, QStringLiteral("registry"));
        QCOMPARE(parsed.package.dependencies.front().registry, QStringLiteral("../registry"));
        QCOMPARE(parsed.package.dependencies.front().version, QStringLiteral("^1.0.0"));

        QFile lockFile(changed.lockFile);
        QVERIFY(lockFile.open(QIODevice::ReadOnly | QIODevice::Text));
        const QJsonDocument doc = QJsonDocument::fromJson(lockFile.readAll());
        QVERIFY(doc.isObject());
        const QJsonObject lockedDep = doc.object().value(QStringLiteral("packages")).toArray().at(0).toObject();
        QCOMPARE(lockedDep.value(QStringLiteral("name")).toString(), QStringLiteral("dep"));
        QCOMPARE(lockedDep.value(QStringLiteral("version")).toString(), QStringLiteral("1.1.0"));
        QCOMPARE(lockedDep.value(QStringLiteral("versionRequirement")).toString(), QStringLiteral("^1.0.0"));
        QCOMPARE(lockedDep.value(QStringLiteral("kind")).toString(), QStringLiteral("registry"));
        QCOMPARE(lockedDep.value(QStringLiteral("source")).toString(), QStringLiteral("../registry"));
        QVERIFY(lockedDep.value(QStringLiteral("resolvedPath")).toString().contains(QStringLiteral(".abel/cache/packages/dep/1.1.0")));
    }

    void publishesPackageToLocalRegistryAndRejectsDuplicateWithoutOverwrite()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QDir root(dir.path());
        QVERIFY(root.mkpath(QStringLiteral("dep/src/lib")));
        QVERIFY(root.mkpath(QStringLiteral("dep/.abel")));
        writeText(root.absoluteFilePath(QStringLiteral("dep/src/main.abel")),
                  QStringLiteral("fn int main() { return 0; }"));
        writeText(root.absoluteFilePath(QStringLiteral("dep/src/lib/value.abel")),
                  QStringLiteral("export fn int dep_value() { return 42; }"));
        writeText(root.absoluteFilePath(QStringLiteral("dep/.abel/ignored.txt")),
                  QStringLiteral("cache must not be published"));
        writeText(root.absoluteFilePath(QStringLiteral("dep/abel.package.json")),
                  QStringLiteral(R"({
                      "name": "dep",
                      "version": "1.2.0",
                      "entry": "src/main.abel"
                  })"));

        const QString depRoot = root.absoluteFilePath(QStringLiteral("dep"));
        const QString registryRoot = root.absoluteFilePath(QStringLiteral("registry"));
        auto published = abel::publishPackageToLocalRegistry(depRoot, registryRoot);
        for (const auto& d : published.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(published.diagnostics.isEmpty());
        QVERIFY(!published.overwritten);
        QCOMPARE(published.package.name, QStringLiteral("dep"));
        QCOMPARE(published.package.version, QStringLiteral("1.2.0"));
        QCOMPARE(published.targetDir, QDir(registryRoot).absoluteFilePath(QStringLiteral("dep/1.2.0")));
        QVERIFY(QFileInfo(QDir(published.targetDir).absoluteFilePath(QStringLiteral("abel.package.json"))).isFile());
        QVERIFY(QFileInfo(QDir(published.targetDir).absoluteFilePath(QStringLiteral("src/lib/value.abel"))).isFile());
        QVERIFY(!QFileInfo(QDir(published.targetDir).absoluteFilePath(QStringLiteral(".abel/ignored.txt"))).exists());

        auto duplicate = abel::publishPackageToLocalRegistry(depRoot, registryRoot);
        QVERIFY(!duplicate.diagnostics.isEmpty());
        bool sawDuplicate = false;
        for (const auto& d : duplicate.diagnostics)
            sawDuplicate = sawDuplicate || d.message.contains(QStringLiteral("already exists"));
        QVERIFY(sawDuplicate);

        writeText(root.absoluteFilePath(QStringLiteral("dep/src/lib/extra.abel")),
                  QStringLiteral("export fn int extra_value() { return 7; }"));
        auto overwritten = abel::publishPackageToLocalRegistry(depRoot, registryRoot, true);
        for (const auto& d : overwritten.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(overwritten.diagnostics.isEmpty());
        QVERIFY(overwritten.overwritten);
        QVERIFY(QFileInfo(QDir(overwritten.targetDir).absoluteFilePath(QStringLiteral("src/lib/extra.abel"))).isFile());
    }

    void indexesAndChecksLocalRegistry()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QDir root(dir.path());
        writePackage(root, QStringLiteral("registry/dep/1.0.0"), QStringLiteral("dep"), QStringLiteral("1.0.0"));
        writePackage(root, QStringLiteral("registry/dep/1.2.0"), QStringLiteral("dep"), QStringLiteral("1.2.0"));
        writePackage(root, QStringLiteral("registry/tool/0.1.0"), QStringLiteral("tool"), QStringLiteral("0.1.0"));

        const QString registryRoot = root.absoluteFilePath(QStringLiteral("registry"));
        auto indexed = abel::writeLocalPackageRegistryIndex(registryRoot);
        for (const auto& d : indexed.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(indexed.diagnostics.isEmpty());
        QVERIFY(indexed.written);
        QCOMPARE(indexed.entries.size(), 3);
        QCOMPARE(indexed.indexFile, QDir(registryRoot).absoluteFilePath(abel::packageLocalRegistryIndexFileName()));
        QVERIFY(QFileInfo(indexed.indexFile).isFile());

        QFile file(indexed.indexFile);
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        QVERIFY(doc.isObject());
        QCOMPARE(doc.object().value(QStringLiteral("kind")).toString(), QStringLiteral("abel.localRegistry"));
        QCOMPARE(doc.object().value(QStringLiteral("packages")).toArray().size(), 3);

        auto checked = abel::checkLocalPackageRegistryIndex(registryRoot);
        for (const auto& d : checked.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(checked.diagnostics.isEmpty());
        QVERIFY(!checked.stale);
        QCOMPARE(checked.entries.size(), 3);

        writePackage(root, QStringLiteral("registry/dep/1.3.0"), QStringLiteral("dep"), QStringLiteral("1.3.0"));
        auto stale = abel::checkLocalPackageRegistryIndex(registryRoot);
        QVERIFY(!stale.diagnostics.isEmpty());
        QVERIFY(stale.stale);
        bool sawStale = false;
        for (const auto& d : stale.diagnostics)
            sawStale = sawStale || d.message.contains(QStringLiteral("stale"));
        QVERIFY(sawStale);
    }

    void rejectsRegistryDirectoryManifestMismatch()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QDir root(dir.path());
        writePackage(root, QStringLiteral("registry/dep/1.0.0"), QStringLiteral("other"), QStringLiteral("1.0.0"));

        auto indexed = abel::scanLocalPackageRegistry(root.absoluteFilePath(QStringLiteral("registry")));
        QVERIFY(!indexed.diagnostics.isEmpty());
        bool sawMismatch = false;
        for (const auto& d : indexed.diagnostics)
            sawMismatch = sawMismatch || d.message.contains(QStringLiteral("contains manifest package"));
        QVERIFY(sawMismatch);
    }

    void registryResolverConsumesIndexAndRejectsStaleIndex()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QDir root(dir.path());
        writePackage(root, QStringLiteral("registry/dep/1.0.0"), QStringLiteral("dep"), QStringLiteral("1.0.0"));
        writePackage(root, QStringLiteral("registry/dep/1.2.0"), QStringLiteral("dep"), QStringLiteral("1.2.0"));
        writePackage(root, QStringLiteral("app"), QStringLiteral("app"), QStringLiteral("0.1.0"));
        writeText(root.absoluteFilePath(QStringLiteral("app/abel.package.json")),
                  QStringLiteral(R"({
                      "name": "app",
                      "version": "0.1.0",
                      "entry": "src/main.abel",
                      "dependencies": [
                          {"name": "dep", "kind": "registry", "registry": "../registry", "version": "^1.0.0"}
                      ]
                  })"));

        const QString registryRoot = root.absoluteFilePath(QStringLiteral("registry"));
        auto indexed = abel::writeLocalPackageRegistryIndex(registryRoot);
        for (const auto& d : indexed.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(indexed.diagnostics.isEmpty());

        const QString appRoot = root.absoluteFilePath(QStringLiteral("app"));
        auto graph = abel::updatePackageGraph(appRoot);
        for (const auto& d : graph.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(graph.diagnostics.isEmpty());
        QCOMPARE(graph.entries.size(), 1);
        QCOMPARE(graph.entries.front().version, QStringLiteral("1.2.0"));

        writePackage(root, QStringLiteral("registry/dep/1.3.0"), QStringLiteral("dep"), QStringLiteral("1.3.0"));
        auto stale = abel::resolvePackageLock(appRoot);
        QVERIFY(!stale.diagnostics.isEmpty());
        bool sawStale = false;
        for (const auto& d : stale.diagnostics)
            sawStale = sawStale || d.message.contains(QStringLiteral("stale"));
        QVERIFY(sawStale);
    }

    void rejectsSharedPathDependencyVersionConflict()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QDir root(dir.path());
        QVERIFY(root.mkpath(QStringLiteral("core/src")));
        QVERIFY(root.mkpath(QStringLiteral("left/src")));
        QVERIFY(root.mkpath(QStringLiteral("right/src")));
        QVERIFY(root.mkpath(QStringLiteral("app/src")));
        writeText(root.absoluteFilePath(QStringLiteral("core/src/main.abel")),
                  QStringLiteral("fn int main() { return 0; }"));
        writeText(root.absoluteFilePath(QStringLiteral("core/abel.package.json")),
                  QStringLiteral(R"({
                      "name": "core",
                      "version": "1.5.0",
                      "entry": "src/main.abel"
                  })"));
        writeText(root.absoluteFilePath(QStringLiteral("left/src/main.abel")),
                  QStringLiteral("fn int main() { return 0; }"));
        writeText(root.absoluteFilePath(QStringLiteral("left/abel.package.json")),
                  QStringLiteral(R"({
                      "name": "left",
                      "version": "0.1.0",
                      "entry": "src/main.abel",
                      "dependencies": [
                          {"name": "core", "kind": "path", "path": "../core", "version": "^1.0.0"}
                      ]
                  })"));
        writeText(root.absoluteFilePath(QStringLiteral("right/src/main.abel")),
                  QStringLiteral("fn int main() { return 0; }"));
        writeText(root.absoluteFilePath(QStringLiteral("right/abel.package.json")),
                  QStringLiteral(R"({
                      "name": "right",
                      "version": "0.1.0",
                      "entry": "src/main.abel",
                      "dependencies": [
                          {"name": "core", "kind": "path", "path": "../core", "version": "^2.0.0"}
                      ]
                  })"));
        writeText(root.absoluteFilePath(QStringLiteral("app/src/main.abel")),
                  QStringLiteral("fn int main() { return 0; }"));
        writeText(root.absoluteFilePath(QStringLiteral("app/abel.package.json")),
                  QStringLiteral(R"({
                      "name": "app",
                      "version": "0.1.0",
                      "entry": "src/main.abel",
                      "dependencies": [
                          {"name": "left", "kind": "path", "path": "../left"},
                          {"name": "right", "kind": "path", "path": "../right"}
                      ]
                  })"));

        auto lock = abel::resolvePackageLock(root.absoluteFilePath(QStringLiteral("app")));
        QVERIFY(!lock.diagnostics.isEmpty());
        bool sawCore = false;
        for (const auto& d : lock.diagnostics)
            sawCore = sawCore || d.message.contains(QStringLiteral("core"));
        QVERIFY(sawCore);
    }

    void rejectsInvalidVersionRequirementSyntax()
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
                          {"name": "dep", "kind": "path", "path": "../dep", "version": "^not-a-version"}
                      ]
                  })"));

        auto parsed = abel::packageManifestFromDirectory(root.absoluteFilePath(QStringLiteral("app")));
        QVERIFY(!parsed.diagnostics.isEmpty());
        bool sawRequirement = false;
        for (const auto& d : parsed.diagnostics)
            sawRequirement = sawRequirement || d.message.contains(QStringLiteral("version requirement"));
        QVERIFY(sawRequirement);
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

    void addsPathDependencyAndUpdatesLockfile()
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
                      "entry": "src/main.abel"
                  })"));

        auto changed = abel::addPathPackageDependency(root.absoluteFilePath(QStringLiteral("app")),
                                                      root.absoluteFilePath(QStringLiteral("dep")));
        for (const auto& d : changed.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(changed.diagnostics.isEmpty());
        QVERIFY(changed.changed);
        QCOMPARE(changed.dependency.name, QStringLiteral("dep"));
        QCOMPARE(changed.dependency.kind, QStringLiteral("path"));
        QCOMPARE(changed.dependency.path, QStringLiteral("../dep"));
        QCOMPARE(changed.lockedPackages, 1);
        QVERIFY(QFileInfo(changed.lockFile).isFile());

        auto parsed = abel::packageManifestFromDirectory(root.absoluteFilePath(QStringLiteral("app")));
        for (const auto& d : parsed.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(parsed.diagnostics.isEmpty());
        QCOMPARE(parsed.package.dependencies.size(), 1);
        QCOMPARE(parsed.package.dependencies.front().name, QStringLiteral("dep"));
        QCOMPARE(parsed.package.dependencies.front().path, QStringLiteral("../dep"));
    }

    void removesDependencyAndUpdatesLockfile()
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

        auto removed = abel::removePackageDependency(root.absoluteFilePath(QStringLiteral("app")),
                                                     QStringLiteral("dep"));
        for (const auto& d : removed.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(removed.diagnostics.isEmpty());
        QVERIFY(removed.changed);
        QCOMPARE(removed.dependency.name, QStringLiteral("dep"));
        QCOMPARE(removed.lockedPackages, 0);
        QVERIFY(QFileInfo(removed.lockFile).isFile());

        QFile manifestFile(root.absoluteFilePath(QStringLiteral("app/abel.package.json")));
        QVERIFY(manifestFile.open(QIODevice::ReadOnly | QIODevice::Text));
        const QJsonDocument manifestDoc = QJsonDocument::fromJson(manifestFile.readAll());
        QVERIFY(manifestDoc.isObject());
        QVERIFY(!manifestDoc.object().contains(QStringLiteral("dependencies")));

        QFile lockFile(removed.lockFile);
        QVERIFY(lockFile.open(QIODevice::ReadOnly | QIODevice::Text));
        const QJsonDocument lockDoc = QJsonDocument::fromJson(lockFile.readAll());
        QVERIFY(lockDoc.isObject());
        QCOMPARE(lockDoc.object().value(QStringLiteral("packages")).toArray().size(), 0);
    }

    void packageGraphCollectsDependencyBackendArtifactsFromLockfile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QDir root(dir.path());
        QVERIFY(root.mkpath(QStringLiteral("dep/src")));
        QVERIFY(root.mkpath(QStringLiteral("dep/plugins")));
        QVERIFY(root.mkpath(QStringLiteral("app/src")));
        writeText(root.absoluteFilePath(QStringLiteral("dep/src/main.abel")),
                  QStringLiteral("fn int main() { return 0; }"));
        writeText(root.absoluteFilePath(QStringLiteral("dep/plugins/libdep_backend.so")),
                  QStringLiteral("placeholder"));
        writeText(root.absoluteFilePath(QStringLiteral("dep/abel.package.json")),
                  QStringLiteral(R"({
                      "name": "dep",
                      "version": "0.2.0",
                      "entry": "src/main.abel",
                      "backendArtifacts": [
                          {
                              "backendId": "DepSystem",
                              "path": "plugins/libdep_backend.so",
                              "symbols": ["ping"]
                          }
                      ]
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

        auto graph = abel::updatePackageGraph(root.absoluteFilePath(QStringLiteral("app")));
        for (const auto& d : graph.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(graph.diagnostics.isEmpty());
        QCOMPARE(graph.root.name, QStringLiteral("app"));
        QCOMPARE(graph.entries.size(), 1);
        QCOMPARE(graph.dependencies.size(), 1);
        QCOMPARE(graph.dependencies.front().name, QStringLiteral("dep"));
        QCOMPARE(graph.backendArtifacts.size(), 1);
        QCOMPARE(graph.backendArtifacts.front().packageName, QStringLiteral("dep"));
        QCOMPARE(graph.backendArtifacts.front().node.backendId, QStringLiteral("DepSystem"));
        QCOMPARE(graph.backendArtifacts.front().node.symbols.front(), QStringLiteral("DepSystem.ping"));
        QVERIFY(QFileInfo(graph.lockFile).isFile());

        auto fromExistingLock = abel::packageGraphFromDirectory(root.absoluteFilePath(QStringLiteral("app")));
        for (const auto& d : fromExistingLock.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(fromExistingLock.diagnostics.isEmpty());
        QCOMPARE(fromExistingLock.entries.size(), 1);
        QCOMPARE(fromExistingLock.backendArtifacts.size(), 1);
        QCOMPARE(fromExistingLock.backendArtifacts.front().packageRoot,
                 root.absoluteFilePath(QStringLiteral("dep")));
    }

    void backendCacheCopiesDependencyArtifacts()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QDir root(dir.path());
        QVERIFY(root.mkpath(QStringLiteral("dep/src")));
        QVERIFY(root.mkpath(QStringLiteral("dep/plugins")));
        QVERIFY(root.mkpath(QStringLiteral("app/src")));
        writeText(root.absoluteFilePath(QStringLiteral("dep/src/main.abel")),
                  QStringLiteral("fn int main() { return 0; }"));
        writeText(root.absoluteFilePath(QStringLiteral("dep/plugins/libdep_backend.so")),
                  QStringLiteral("dep-binary"));
        writeText(root.absoluteFilePath(QStringLiteral("dep/abel.package.json")),
                  QStringLiteral(R"({
                      "name": "dep",
                      "version": "0.2.0",
                      "entry": "src/main.abel",
                      "backendArtifacts": [
                          {
                              "backendId": "DepSystem",
                              "path": "plugins/libdep_backend.so",
                              "symbols": ["ping"]
                          }
                      ]
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

        const QString appRoot = root.absoluteFilePath(QStringLiteral("app"));
        auto graph = abel::updatePackageGraph(appRoot);
        for (const auto& d : graph.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(graph.diagnostics.isEmpty());

        auto cache = abel::updatePackageBackendCache(graph);
        for (const auto& d : cache.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(cache.diagnostics.isEmpty());
        QCOMPARE(cache.rootDir, appRoot);
        QCOMPARE(cache.cacheDir, abel::packageBackendCacheDir(appRoot));
        QCOMPARE(cache.resources.size(), 1);

        const QString expectedCachedPath = QDir(appRoot).absoluteFilePath(
            QStringLiteral(".abel/cache/backend/dep/DepSystem/libdep_backend.so"));
        QCOMPARE(cache.resources.front().packageName, QStringLiteral("dep"));
        QCOMPARE(cache.resources.front().cachedPath, expectedCachedPath);
        QCOMPARE(cache.resources.front().metadataPath, expectedCachedPath + QStringLiteral(".abel-cache.json"));
        QCOMPARE(cache.resources.front().node.path, expectedCachedPath);
        QVERIFY(QFileInfo(expectedCachedPath).isFile());
        QVERIFY(QFileInfo(cache.resources.front().metadataPath).isFile());

        QFile copied(expectedCachedPath);
        QVERIFY(copied.open(QIODevice::ReadOnly | QIODevice::Text));
        QCOMPARE(QString::fromUtf8(copied.readAll()), QStringLiteral("dep-binary"));

        QFile metadata(cache.resources.front().metadataPath);
        QVERIFY(metadata.open(QIODevice::ReadOnly | QIODevice::Text));
        const QJsonDocument metadataDoc = QJsonDocument::fromJson(metadata.readAll());
        QVERIFY(metadataDoc.isObject());
        const QJsonObject metadataObject = metadataDoc.object();
        QCOMPARE(metadataObject.value(QStringLiteral("formatVersion")).toInt(), 2);
        QCOMPARE(metadataObject.value(QStringLiteral("qtVersion")).toString(), abel::currentAbelQtVersion());
        QCOMPARE(metadataObject.value(QStringLiteral("kit")).toString(), abel::currentAbelQtKit());
        QCOMPARE(metadataObject.value(QStringLiteral("platform")).toString(), abel::currentAbelPlatform());
        QCOMPARE(metadataObject.value(QStringLiteral("compiler")).toString(), abel::currentAbelCompiler());
        QCOMPARE(metadataObject.value(QStringLiteral("compilerVersion")).toString(), abel::currentAbelCompilerVersion());
        QCOMPARE(metadataObject.value(QStringLiteral("cxxStandard")).toString(), abel::currentAbelCxxStandard());
        QCOMPARE(metadataObject.value(QStringLiteral("abelAbi")).toString(), abel::currentAbelAbi());

        const QList<abel::PackageResolvedResource> runResources = abel::cachedPackageBackendArtifacts(graph);
        QCOMPARE(runResources.size(), 1);
        QCOMPARE(runResources.front().packageRoot, appRoot);
        QCOMPARE(runResources.front().node.path, expectedCachedPath);
    }

    void backendCacheFallsBackWhenMetadataIsStale()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QDir root(dir.path());
        QVERIFY(root.mkpath(QStringLiteral("dep/src")));
        QVERIFY(root.mkpath(QStringLiteral("dep/plugins")));
        QVERIFY(root.mkpath(QStringLiteral("app/src")));
        writeText(root.absoluteFilePath(QStringLiteral("dep/src/main.abel")),
                  QStringLiteral("fn int main() { return 0; }"));
        const QString sourceArtifact = root.absoluteFilePath(QStringLiteral("dep/plugins/libdep_backend.so"));
        writeText(sourceArtifact, QStringLiteral("dep-binary"));
        writeText(root.absoluteFilePath(QStringLiteral("dep/abel.package.json")),
                  QStringLiteral(R"({
                      "name": "dep",
                      "version": "0.2.0",
                      "entry": "src/main.abel",
                      "backendArtifacts": [
                          {
                              "backendId": "DepSystem",
                              "path": "plugins/libdep_backend.so",
                              "symbols": ["ping"]
                          }
                      ]
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

        const QString appRoot = root.absoluteFilePath(QStringLiteral("app"));
        const QString depRoot = root.absoluteFilePath(QStringLiteral("dep"));
        auto graph = abel::updatePackageGraph(appRoot);
        QVERIFY(graph.diagnostics.isEmpty());

        auto cache = abel::updatePackageBackendCache(graph);
        for (const auto& d : cache.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(cache.diagnostics.isEmpty());
        QCOMPARE(cache.resources.size(), 1);

        const QString expectedCachedPath = QDir(appRoot).absoluteFilePath(
            QStringLiteral(".abel/cache/backend/dep/DepSystem/libdep_backend.so"));
        const QString expectedMetadataPath = expectedCachedPath + QStringLiteral(".abel-cache.json");
        QVERIFY(QFileInfo(expectedCachedPath).isFile());
        QVERIFY(QFileInfo(expectedMetadataPath).isFile());

        QList<abel::PackageResolvedResource> runResources = abel::cachedPackageBackendArtifacts(graph);
        QCOMPARE(runResources.size(), 1);
        QCOMPARE(runResources.front().packageRoot, appRoot);
        QCOMPARE(runResources.front().node.path, expectedCachedPath);

        writeText(sourceArtifact, QStringLiteral("dep-binary-updated-and-larger"));

        runResources = abel::cachedPackageBackendArtifacts(graph);
        QCOMPARE(runResources.size(), 1);
        QCOMPARE(runResources.front().packageRoot, depRoot);
        QCOMPARE(runResources.front().node.path, QStringLiteral("plugins/libdep_backend.so"));
    }

    void backendCacheFallsBackWhenCompatibilityMetadataIsStale()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QDir root(dir.path());
        QVERIFY(root.mkpath(QStringLiteral("dep/src")));
        QVERIFY(root.mkpath(QStringLiteral("dep/plugins")));
        QVERIFY(root.mkpath(QStringLiteral("app/src")));
        writeText(root.absoluteFilePath(QStringLiteral("dep/src/main.abel")),
                  QStringLiteral("fn int main() { return 0; }"));
        writeText(root.absoluteFilePath(QStringLiteral("dep/plugins/libdep_backend.so")),
                  QStringLiteral("dep-binary"));
        writeText(root.absoluteFilePath(QStringLiteral("dep/abel.package.json")),
                  QStringLiteral(R"({
                      "name": "dep",
                      "version": "0.2.0",
                      "entry": "src/main.abel",
                      "backendArtifacts": [
                          {
                              "backendId": "DepSystem",
                              "path": "plugins/libdep_backend.so",
                              "symbols": ["ping"]
                          }
                      ]
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

        const QString appRoot = root.absoluteFilePath(QStringLiteral("app"));
        const QString depRoot = root.absoluteFilePath(QStringLiteral("dep"));
        auto graph = abel::updatePackageGraph(appRoot);
        QVERIFY(graph.diagnostics.isEmpty());

        auto cache = abel::updatePackageBackendCache(graph);
        for (const auto& d : cache.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(cache.diagnostics.isEmpty());
        QCOMPARE(cache.resources.size(), 1);

        const QString cachedPath = cache.resources.front().cachedPath;
        const QString metadataPath = cache.resources.front().metadataPath;
        QFile metadata(metadataPath);
        QVERIFY(metadata.open(QIODevice::ReadOnly | QIODevice::Text));
        QJsonDocument doc = QJsonDocument::fromJson(metadata.readAll());
        metadata.close();
        QVERIFY(doc.isObject());
        QJsonObject object = doc.object();
        object.insert(QStringLiteral("compilerVersion"), QStringLiteral("foreign"));
        metadata.setFileName(metadataPath);
        QVERIFY(metadata.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
        const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
        QCOMPARE(metadata.write(bytes), static_cast<qint64>(bytes.size()));

        QList<abel::PackageResolvedResource> runResources = abel::cachedPackageBackendArtifacts(graph);
        QCOMPARE(runResources.size(), 1);
        QCOMPARE(runResources.front().packageRoot, depRoot);
        QCOMPARE(runResources.front().node.path, QStringLiteral("plugins/libdep_backend.so"));
        QVERIFY(QFileInfo(cachedPath).isFile());
    }

    void backendCacheBuildsCmakeArtifactsBeforeCopy()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QDir root(dir.path());
        QVERIFY(root.mkpath(QStringLiteral("dep/src")));
        QVERIFY(root.mkpath(QStringLiteral("dep/backend")));
        QVERIFY(root.mkpath(QStringLiteral("app/src")));
        writeText(root.absoluteFilePath(QStringLiteral("dep/src/main.abel")),
                  QStringLiteral("fn int main() { return 0; }"));
        writeText(root.absoluteFilePath(QStringLiteral("dep/backend/CMakeLists.txt")),
                  QStringLiteral(R"(
cmake_minimum_required(VERSION 3.30)
project(DepBackendFixture LANGUAGES CXX)
add_library(dep_backend MODULE dep_backend.cpp)
set_target_properties(dep_backend PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    PREFIX "lib"
)
)"));
        writeText(root.absoluteFilePath(QStringLiteral("dep/backend/dep_backend.cpp")),
                  QStringLiteral(R"(
extern "C" int dep_backend_answer() {
    return 42;
}
)"));
        writeText(root.absoluteFilePath(QStringLiteral("dep/abel.package.json")),
                  QStringLiteral(R"({
                      "name": "dep",
                      "version": "0.2.0",
                      "entry": "src/main.abel",
                      "backendArtifacts": [
                          {
                              "backendId": "DepSystem",
                              "path": "build/backend/plugins/libdep_backend.so",
                              "symbols": ["ping"],
                              "build": {
                                  "system": "cmake",
                                  "cmake": ")")
                      + QStringLiteral(ABEL_TEST_CMAKE_EXECUTABLE).replace(QStringLiteral("\\"), QStringLiteral("\\\\"))
                      + QStringLiteral(R"(",
                                  "source": "backend",
                                  "buildDir": "build/backend",
                                  "generator": "Ninja"
                              }
                          }
                      ]
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

        const QString appRoot = root.absoluteFilePath(QStringLiteral("app"));
        auto graph = abel::updatePackageGraph(appRoot);
        for (const auto& d : graph.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(graph.diagnostics.isEmpty());
        QCOMPARE(graph.backendArtifacts.size(), 1);
        QVERIFY(graph.backendArtifacts.front().build.enabled);
        QCOMPARE(graph.backendArtifacts.front().build.system, QStringLiteral("cmake"));

        const QString sourceArtifact = root.absoluteFilePath(QStringLiteral("dep/build/backend/plugins/libdep_backend.so"));
        QVERIFY(!QFileInfo(sourceArtifact).exists());

        auto cache = abel::updatePackageBackendCache(graph);
        for (const auto& d : cache.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(cache.diagnostics.isEmpty());
        QCOMPARE(cache.resources.size(), 1);
        QVERIFY(QFileInfo(sourceArtifact).isFile());

        const QString expectedCachedPath = QDir(appRoot).absoluteFilePath(
            QStringLiteral(".abel/cache/backend/dep/DepSystem/libdep_backend.so"));
        QCOMPARE(cache.resources.front().sourcePath, sourceArtifact);
        QCOMPARE(cache.resources.front().cachedPath, expectedCachedPath);
        QVERIFY(QFileInfo(expectedCachedPath).isFile());
    }

    void backendCacheDiagnosesMissingArtifacts()
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
                      "entry": "src/main.abel",
                      "backendArtifacts": [
                          {
                              "backendId": "DepSystem",
                              "path": "plugins/missing_backend.so",
                              "symbols": ["ping"]
                          }
                      ]
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

        auto graph = abel::updatePackageGraph(root.absoluteFilePath(QStringLiteral("app")));
        QVERIFY(graph.diagnostics.isEmpty());
        QCOMPARE(graph.backendArtifacts.size(), 1);

        auto cache = abel::updatePackageBackendCache(graph);
        QVERIFY(!cache.diagnostics.isEmpty());
        QCOMPARE(cache.resources.size(), 0);
        bool sawMissing = false;
        for (const auto& d : cache.diagnostics)
            sawMissing = sawMissing || d.message.contains(QStringLiteral("does not exist"));
        QVERIFY(sawMissing);
    }

    void resolverRejectsSamePackageNameWithDifferentResolutions()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QDir root(dir.path());
        writePackage(root, QStringLiteral("shared_v1"), QStringLiteral("shared"), QStringLiteral("1.0.0"));
        writePackage(root, QStringLiteral("shared_v2"), QStringLiteral("shared"), QStringLiteral("2.0.0"));
        writePackage(root, QStringLiteral("a"), QStringLiteral("a"), QStringLiteral("0.1.0"));
        writePackage(root, QStringLiteral("b"), QStringLiteral("b"), QStringLiteral("0.1.0"));
        QVERIFY(root.mkpath(QStringLiteral("app/src")));
        writeText(root.absoluteFilePath(QStringLiteral("app/src/main.abel")),
                  QStringLiteral("fn int main() { return 0; }"));

        writeText(root.absoluteFilePath(QStringLiteral("a/abel.package.json")),
                  QStringLiteral(R"({
                      "name": "a",
                      "version": "0.1.0",
                      "entry": "src/main.abel",
                      "dependencies": [
                          {"name": "shared", "kind": "path", "path": "../shared_v1"}
                      ]
                  })"));
        writeText(root.absoluteFilePath(QStringLiteral("b/abel.package.json")),
                  QStringLiteral(R"({
                      "name": "b",
                      "version": "0.1.0",
                      "entry": "src/main.abel",
                      "dependencies": [
                          {"name": "shared", "kind": "path", "path": "../shared_v2"}
                      ]
                  })"));
        writeText(root.absoluteFilePath(QStringLiteral("app/abel.package.json")),
                  QStringLiteral(R"({
                      "name": "app",
                      "version": "0.1.0",
                      "entry": "src/main.abel",
                      "dependencies": [
                          {"name": "a", "kind": "path", "path": "../a"},
                          {"name": "b", "kind": "path", "path": "../b"}
                      ]
                  })"));

        auto lock = abel::resolvePackageLock(root.absoluteFilePath(QStringLiteral("app")));
        QVERIFY(!lock.diagnostics.isEmpty());
        bool sawConflict = false;
        for (const auto& d : lock.diagnostics)
            sawConflict = sawConflict || d.message.contains(QStringLiteral("dependency conflict for package 'shared'"));
        QVERIFY(sawConflict);
    }

    void packageGraphRejectsStaleLockfile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QDir root(dir.path());
        QVERIFY(root.mkpath(QStringLiteral("dep/src")));
        QVERIFY(root.mkpath(QStringLiteral("other/src")));
        QVERIFY(root.mkpath(QStringLiteral("app/src")));
        writeText(root.absoluteFilePath(QStringLiteral("dep/src/main.abel")),
                  QStringLiteral("fn int main() { return 0; }"));
        writeText(root.absoluteFilePath(QStringLiteral("dep/abel.package.json")),
                  QStringLiteral(R"({
                      "name": "dep",
                      "version": "0.2.0",
                      "entry": "src/main.abel"
                  })"));
        writeText(root.absoluteFilePath(QStringLiteral("other/src/main.abel")),
                  QStringLiteral("fn int main() { return 0; }"));
        writeText(root.absoluteFilePath(QStringLiteral("other/abel.package.json")),
                  QStringLiteral(R"({
                      "name": "other",
                      "version": "0.3.0",
                      "entry": "src/main.abel"
                  })"));
        const QString appManifest = root.absoluteFilePath(QStringLiteral("app/abel.package.json"));
        writeText(root.absoluteFilePath(QStringLiteral("app/src/main.abel")),
                  QStringLiteral("fn int main() { return 0; }"));
        writeText(appManifest,
                  QStringLiteral(R"({
                      "name": "app",
                      "version": "0.1.0",
                      "entry": "src/main.abel",
                      "dependencies": [
                          {"name": "dep", "kind": "path", "path": "../dep"}
                      ]
                  })"));

        auto graph = abel::updatePackageGraph(root.absoluteFilePath(QStringLiteral("app")));
        QVERIFY(graph.diagnostics.isEmpty());
        QCOMPARE(graph.entries.size(), 1);
        QCOMPARE(graph.entries.front().name, QStringLiteral("dep"));

        writeText(appManifest,
                  QStringLiteral(R"({
                      "name": "app",
                      "version": "0.1.0",
                      "entry": "src/main.abel",
                      "dependencies": [
                          {"name": "other", "kind": "path", "path": "../other"}
                      ]
                  })"));

        auto stale = abel::packageGraphFromDirectory(root.absoluteFilePath(QStringLiteral("app")));
        QVERIFY(!stale.diagnostics.isEmpty());
        bool sawStale = false;
        for (const auto& d : stale.diagnostics)
            sawStale = sawStale || d.message.contains(QStringLiteral("stale"));
        QVERIFY(sawStale);
    }

    void packageGraphRejectsStaleLockfileWhenVersionRequirementChanges()
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
                      "version": "0.2.3",
                      "entry": "src/main.abel"
                  })"));
        const QString appManifest = root.absoluteFilePath(QStringLiteral("app/abel.package.json"));
        writeText(root.absoluteFilePath(QStringLiteral("app/src/main.abel")),
                  QStringLiteral("fn int main() { return 0; }"));
        writeText(appManifest,
                  QStringLiteral(R"({
                      "name": "app",
                      "version": "0.1.0",
                      "entry": "src/main.abel",
                      "dependencies": [
                          {"name": "dep", "kind": "path", "path": "../dep", "version": "^0.2.0"}
                      ]
                  })"));

        auto graph = abel::updatePackageGraph(root.absoluteFilePath(QStringLiteral("app")));
        QVERIFY(graph.diagnostics.isEmpty());
        QCOMPARE(graph.entries.size(), 1);
        QCOMPARE(graph.entries.front().versionRequirement, QStringLiteral("^0.2.0"));

        writeText(appManifest,
                  QStringLiteral(R"({
                      "name": "app",
                      "version": "0.1.0",
                      "entry": "src/main.abel",
                      "dependencies": [
                          {"name": "dep", "kind": "path", "path": "../dep", "version": "~0.2.0"}
                      ]
                  })"));

        auto stale = abel::packageGraphFromDirectory(root.absoluteFilePath(QStringLiteral("app")));
        QVERIFY(!stale.diagnostics.isEmpty());
        bool sawStale = false;
        for (const auto& d : stale.diagnostics)
            sawStale = sawStale || d.message.contains(QStringLiteral("stale"));
        QVERIFY(sawStale);
    }
};

QTEST_MAIN(AbelPackageManifestTests)

#include "test_package_manifest.moc"
