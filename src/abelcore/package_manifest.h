#pragma once

#include "abelcore/diagnostic.h"
#include "abelcore/resource_node.h"

#include <QJsonObject>
#include <QList>
#include <QString>

namespace abel {

struct PackageDependency {
    QString name;
    QString kind;
    QString path;
    QString version;
};

struct PackageBackendBuildSpec {
    bool enabled = false;
    QString system = QStringLiteral("cmake");
    QString cmake;
    QString source;
    QString buildDir;
    QString generator;
    QString target;
    QStringList configureArgs;
    QStringList buildArgs;
};

struct PackageManifest {
    QString name;
    QString version;
    QString entry;
    QString rootDir;
    QString filePath;
    QList<ResourceNode> backendArtifacts;
    QList<PackageBackendBuildSpec> backendArtifactBuilds;
    QList<PackageDependency> dependencies;

    QString entryFilePath() const;
};

struct PackageManifestParseResult {
    PackageManifest package;
    QList<Diagnostic> diagnostics;

    bool ok() const { return diagnostics.isEmpty(); }
};

struct PackageInitOptions {
    QString rootDir;
    QString name;
    QString version = QStringLiteral("0.1.0");
};

struct PackageInitResult {
    QString rootDir;
    QList<QString> createdFiles;
    QList<Diagnostic> diagnostics;

    bool ok() const { return diagnostics.isEmpty(); }
};

struct PackageLockEntry {
    QString name;
    QString version;
    QString kind;
    QString source;
    QString resolvedPath;
};

struct PackageLockResult {
    QString rootDir;
    QString rootName;
    QString rootVersion;
    QString lockFile;
    QList<PackageLockEntry> entries;
    QList<Diagnostic> diagnostics;

    bool ok() const { return diagnostics.isEmpty(); }
};

struct PackageDependencyChangeResult {
    QString rootDir;
    QString manifestFile;
    QString lockFile;
    PackageDependency dependency;
    int lockedPackages = 0;
    bool changed = false;
    QList<Diagnostic> diagnostics;

    bool ok() const { return diagnostics.isEmpty(); }
};

struct PackageResolvedResource {
    QString packageName;
    QString packageRoot;
    ResourceNode node;
    PackageBackendBuildSpec build;
};

struct PackageCachedResource {
    QString packageName;
    QString packageRoot;
    QString sourcePath;
    QString cachedPath;
    ResourceNode node;
};

struct PackageGraphResult {
    PackageManifest root;
    QString lockFile;
    QList<PackageLockEntry> entries;
    QList<PackageManifest> dependencies;
    QList<PackageResolvedResource> backendArtifacts;
    QList<Diagnostic> diagnostics;

    bool ok() const { return diagnostics.isEmpty(); }
};

struct PackageBackendCacheResult {
    QString rootDir;
    QString cacheDir;
    QList<PackageCachedResource> resources;
    QList<Diagnostic> diagnostics;

    bool ok() const { return diagnostics.isEmpty(); }
};

QString packageManifestFileName();
QString packageLockFileName();
QString packageCacheRoot(const QString& rootDir);
QString packageBackendCacheDir(const QString& rootDir);
bool isPackageDirectory(const QString& path);

PackageInitResult initPackageProject(const PackageInitOptions& options);
PackageLockResult resolvePackageLock(const QString& dir);
PackageLockResult updatePackageLock(const QString& dir);
PackageLockResult packageLockFromFile(const QString& lockFile);
PackageGraphResult packageGraphFromDirectory(const QString& dir);
PackageGraphResult updatePackageGraph(const QString& dir);
PackageBackendCacheResult updatePackageBackendCache(const PackageGraphResult& graph);
QList<PackageResolvedResource> cachedPackageBackendArtifacts(const PackageGraphResult& graph);
PackageDependencyChangeResult addPathPackageDependency(const QString& dir, const QString& dependencyDir);
PackageDependencyChangeResult removePackageDependency(const QString& dir, const QString& dependencyName);
PackageManifestParseResult packageManifestFromJson(const QJsonObject& object,
                                                   const QString& rootDir = {},
                                                   const SourceSpan& span = {});
PackageManifestParseResult packageManifestFromJsonText(const QString& text,
                                                       const QString& file = {},
                                                       const QString& rootDir = {});
PackageManifestParseResult packageManifestFromFile(const QString& manifestFile);
PackageManifestParseResult packageManifestFromDirectory(const QString& dir);

} // namespace abel
