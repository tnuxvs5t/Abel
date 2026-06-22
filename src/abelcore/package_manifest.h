#pragma once

#include "abelcore/diagnostic.h"
#include "abelcore/resource_node.h"

#include <QJsonObject>
#include <QList>
#include <QString>

namespace abel {

struct PackageManifest {
    QString name;
    QString version;
    QString entry;
    QString rootDir;
    QString filePath;
    QList<ResourceNode> backendArtifacts;

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

QString packageManifestFileName();
bool isPackageDirectory(const QString& path);

PackageInitResult initPackageProject(const PackageInitOptions& options);
PackageManifestParseResult packageManifestFromJson(const QJsonObject& object,
                                                   const QString& rootDir = {},
                                                   const SourceSpan& span = {});
PackageManifestParseResult packageManifestFromJsonText(const QString& text,
                                                       const QString& file = {},
                                                       const QString& rootDir = {});
PackageManifestParseResult packageManifestFromFile(const QString& manifestFile);
PackageManifestParseResult packageManifestFromDirectory(const QString& dir);

} // namespace abel
