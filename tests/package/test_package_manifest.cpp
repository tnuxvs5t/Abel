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
        QCOMPARE(cache.resources.front().node.path, expectedCachedPath);
        QVERIFY(QFileInfo(expectedCachedPath).isFile());

        QFile copied(expectedCachedPath);
        QVERIFY(copied.open(QIODevice::ReadOnly | QIODevice::Text));
        QCOMPARE(QString::fromUtf8(copied.readAll()), QStringLiteral("dep-binary"));

        const QList<abel::PackageResolvedResource> runResources = abel::cachedPackageBackendArtifacts(graph);
        QCOMPARE(runResources.size(), 1);
        QCOMPARE(runResources.front().packageRoot, appRoot);
        QCOMPARE(runResources.front().node.path, expectedCachedPath);
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
};

QTEST_MAIN(AbelPackageManifestTests)

#include "test_package_manifest.moc"
