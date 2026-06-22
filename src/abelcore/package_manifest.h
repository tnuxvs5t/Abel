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

struct PackageManifest {
    QString name;
    QString version;
    QString entry;
    QString rootDir;
    QString filePath;
    QList<ResourceNode> backendArtifacts;
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

QString packageManifestFileName();
QString packageLockFileName();
bool isPackageDirectory(const QString& path);

PackageInitResult initPackageProject(const PackageInitOptions& options);
PackageLockResult resolvePackageLock(const QString& dir);
PackageLockResult updatePackageLock(const QString& dir);
PackageManifestParseResult packageManifestFromJson(const QJsonObject& object,
                                                   const QString& rootDir = {},
                                                   const SourceSpan& span = {});
PackageManifestParseResult packageManifestFromJsonText(const QString& text,
                                                       const QString& file = {},
                                                       const QString& rootDir = {});
PackageManifestParseResult packageManifestFromFile(const QString& manifestFile);
PackageManifestParseResult packageManifestFromDirectory(const QString& dir);

} // namespace abel
