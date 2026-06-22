#include "abelcore/package_manifest.h"

#include "abelcore/backend_interface.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSet>
#include <QTextStream>
#include <QtGlobal>

namespace abel {

namespace {

constexpr auto kPackageManifestFileName = "abel.package.json";
constexpr auto kPackageLockFileName = "abel.lock.json";

void addPackageError(QList<Diagnostic>& diagnostics, const QString& message, const SourceSpan& span)
{
    Diagnostic d;
    d.severity = Severity::Error;
    d.code = QStringLiteral("E0801");
    d.message = message;
    d.primary = span;
    diagnostics.push_back(d);
}

QString defaultPackageNameForDir(const QString& rootDir)
{
    const QString name = QFileInfo(rootDir).fileName().trimmed();
    if (!name.isEmpty())
        return name;
    return QStringLiteral("abel-project");
}

bool writeNewTextFile(const QString& path,
                      const QString& text,
                      QList<QString>& createdFiles,
                      QList<Diagnostic>& diagnostics)
{
    SourceSpan span;
    span.file = path;
    if (QFileInfo::exists(path)) {
        addPackageError(diagnostics, QStringLiteral("refusing to overwrite existing file '%1'").arg(path), span);
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        addPackageError(diagnostics, QStringLiteral("cannot create file '%1'").arg(path), span);
        return false;
    }
    const QByteArray bytes = text.toUtf8();
    if (file.write(bytes) != bytes.size()) {
        addPackageError(diagnostics, QStringLiteral("failed to write file '%1'").arg(path), span);
        return false;
    }
    createdFiles.push_back(path);
    return true;
}

QString requiredString(const QJsonObject& object,
                       const QString& key,
                       QList<Diagnostic>& diagnostics,
                       const SourceSpan& span)
{
    const QJsonValue value = object.value(key);
    if (!value.isString() || value.toString().trimmed().isEmpty()) {
        addPackageError(diagnostics, QStringLiteral("package manifest requires non-empty string field '%1'").arg(key), span);
        return {};
    }
    return value.toString().trimmed();
}

QString optionalString(const QJsonObject& object, const QString& key, const QString& fallback)
{
    const QJsonValue value = object.value(key);
    if (value.isString() && !value.toString().trimmed().isEmpty())
        return value.toString().trimmed();
    return fallback;
}

QString absolutePathFrom(const QString& rootDir, const QString& path)
{
    QFileInfo info(path);
    if (info.isRelative())
        info = QFileInfo(QDir(rootDir).absoluteFilePath(path));
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

PackageDependency parseDependency(const QJsonObject& object,
                                  QList<Diagnostic>& diagnostics,
                                  const SourceSpan& span)
{
    PackageDependency dependency;
    dependency.name = requiredString(object, QStringLiteral("name"), diagnostics, span);
    dependency.kind = requiredString(object, QStringLiteral("kind"), diagnostics, span);
    dependency.version = optionalString(object, QStringLiteral("version"), QString());
    if (dependency.kind != QStringLiteral("path")) {
        addPackageError(diagnostics,
                        QStringLiteral("dependency '%1' kind '%2' is not supported yet; only 'path' is supported")
                            .arg(dependency.name, dependency.kind),
                        span);
        return dependency;
    }
    dependency.path = requiredString(object, QStringLiteral("path"), diagnostics, span);
    return dependency;
}

QStringList parseSymbols(const QJsonObject& object,
                         const QString& backendId,
                         QList<Diagnostic>& diagnostics,
                         const SourceSpan& span)
{
    QStringList out;
    const QJsonValue value = object.value(QStringLiteral("symbols"));
    if (!value.isArray() || value.toArray().isEmpty()) {
        addPackageError(diagnostics, QStringLiteral("backend artifact requires non-empty array field 'symbols'"), span);
        return out;
    }
    for (const QJsonValue& raw : value.toArray()) {
        if (!raw.isString() || raw.toString().trimmed().isEmpty()) {
            addPackageError(diagnostics, QStringLiteral("backend artifact symbols must be non-empty strings"), span);
            continue;
        }
        QString symbol = raw.toString().trimmed();
        if (!symbol.contains(QStringLiteral(".")))
            symbol = backendId + QStringLiteral(".") + symbol;
        if (!backendId.isEmpty() && !symbol.startsWith(backendId + QStringLiteral("."))) {
            addPackageError(diagnostics,
                            QStringLiteral("backend artifact symbol '%1' does not belong to backend '%2'")
                                .arg(symbol, backendId),
                            span);
            continue;
        }
        out.push_back(symbol);
    }
    return out;
}

ResourceNode parseBackendArtifact(const QJsonObject& object,
                                  QList<Diagnostic>& diagnostics,
                                  const SourceSpan& span)
{
    ResourceNode node;
    node.backendId = requiredString(object, QStringLiteral("backendId"), diagnostics, span);
    node.path = requiredString(object, QStringLiteral("path"), diagnostics, span);
    node.id = optionalString(object,
                             QStringLiteral("id"),
                             node.backendId.isEmpty()
                                 ? QStringLiteral("backend.artifact")
                                 : node.backendId.toLower() + QStringLiteral(".backend"));
    node.kind = optionalString(object, QStringLiteral("kind"), QStringLiteral("qt_plugin"));
    node.iid = optionalString(object, QStringLiteral("iid"), QStringLiteral(IAbelBackend_iid));
    node.qtVersion = optionalString(object, QStringLiteral("qtVersion"), QString::fromLatin1(qVersion()));
    node.kit = optionalString(object, QStringLiteral("kit"), QStringLiteral("gcc_64"));
    node.symbols = parseSymbols(object, node.backendId, diagnostics, span);
    node.state = ResourceNodeState::Unloaded;

    if (node.kind != QStringLiteral("qt_plugin"))
        addPackageError(diagnostics, QStringLiteral("backend artifact kind must be 'qt_plugin'"), span);
    if (node.iid != QStringLiteral(IAbelBackend_iid))
        addPackageError(diagnostics,
                        QStringLiteral("backend artifact iid must be '%1'").arg(QStringLiteral(IAbelBackend_iid)),
                        span);
    return node;
}

void resolvePackageDependencies(const PackageManifest& package,
                                PackageLockResult& result,
                                QSet<QString>& seen,
                                QSet<QString>& resolving)
{
    for (const PackageDependency& dependency : package.dependencies) {
        SourceSpan span;
        span.file = package.filePath;
        if (dependency.kind != QStringLiteral("path")) {
            addPackageError(result.diagnostics,
                            QStringLiteral("dependency '%1' kind '%2' is not supported yet")
                                .arg(dependency.name, dependency.kind),
                            span);
            continue;
        }

        const QString resolvedPath = absolutePathFrom(package.rootDir, dependency.path);
        const QFileInfo depInfo(resolvedPath);
        if (!depInfo.isDir() || !isPackageDirectory(resolvedPath)) {
            addPackageError(result.diagnostics,
                            QStringLiteral("path dependency '%1' does not point to an Abel package: %2")
                                .arg(dependency.name, dependency.path),
                            span);
            continue;
        }

        const QString key = absolutePathFrom(QString(), resolvedPath);
        if (resolving.contains(key)) {
            addPackageError(result.diagnostics,
                            QStringLiteral("circular path dependency involving '%1' at %2")
                                .arg(dependency.name, resolvedPath),
                            span);
            continue;
        }
        if (seen.contains(key))
            continue;

        auto parsed = packageManifestFromDirectory(resolvedPath);
        result.diagnostics.append(parsed.diagnostics);
        if (!parsed.ok())
            continue;
        if (!dependency.name.isEmpty() && parsed.package.name != dependency.name) {
            addPackageError(result.diagnostics,
                            QStringLiteral("path dependency name mismatch: manifest asks for '%1' but package is '%2'")
                                .arg(dependency.name, parsed.package.name),
                            span);
            continue;
        }

        PackageLockEntry entry;
        entry.name = parsed.package.name;
        entry.version = parsed.package.version;
        entry.kind = dependency.kind;
        entry.source = dependency.path;
        entry.resolvedPath = key;
        result.entries.push_back(entry);

        seen.insert(key);
        resolving.insert(key);
        resolvePackageDependencies(parsed.package, result, seen, resolving);
        resolving.remove(key);
    }
}

QJsonObject lockEntryToJson(const PackageLockEntry& entry)
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), entry.name);
    object.insert(QStringLiteral("version"), entry.version);
    object.insert(QStringLiteral("kind"), entry.kind);
    object.insert(QStringLiteral("source"), entry.source);
    object.insert(QStringLiteral("resolvedPath"), entry.resolvedPath);
    return object;
}

QJsonObject dependencyToJson(const PackageDependency& dependency)
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), dependency.name);
    object.insert(QStringLiteral("kind"), dependency.kind);
    if (!dependency.path.isEmpty())
        object.insert(QStringLiteral("path"), dependency.path);
    if (!dependency.version.isEmpty())
        object.insert(QStringLiteral("version"), dependency.version);
    return object;
}

QJsonDocument packageLockToJson(const PackageLockResult& lock)
{
    QJsonObject root;
    root.insert(QStringLiteral("formatVersion"), 1);

    QJsonObject rootPackage;
    rootPackage.insert(QStringLiteral("name"), lock.rootName);
    rootPackage.insert(QStringLiteral("version"), lock.rootVersion);
    rootPackage.insert(QStringLiteral("path"), lock.rootDir);
    root.insert(QStringLiteral("root"), rootPackage);

    QJsonArray packages;
    for (const PackageLockEntry& entry : lock.entries)
        packages.push_back(lockEntryToJson(entry));
    root.insert(QStringLiteral("packages"), packages);
    return QJsonDocument(root);
}

bool readManifestObjectForUpdate(const QString& dir,
                                 QJsonObject& object,
                                 QString& manifestFile,
                                 QString& rootDir,
                                 QList<Diagnostic>& diagnostics)
{
    const QFileInfo rootInfo(dir);
    if (!rootInfo.isDir()) {
        SourceSpan span;
        span.file = dir;
        addPackageError(diagnostics, QStringLiteral("package path '%1' is not a directory").arg(dir), span);
        return false;
    }

    rootDir = rootInfo.absoluteFilePath();
    manifestFile = QDir(rootDir).absoluteFilePath(packageManifestFileName());
    QFile file(manifestFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        SourceSpan span;
        span.file = manifestFile;
        addPackageError(diagnostics, QStringLiteral("cannot open package manifest '%1'").arg(manifestFile), span);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    SourceSpan span;
    span.file = manifestFile;
    if (parseError.error != QJsonParseError::NoError) {
        addPackageError(diagnostics, QStringLiteral("invalid package manifest JSON: %1").arg(parseError.errorString()), span);
        return false;
    }
    if (!doc.isObject()) {
        addPackageError(diagnostics, QStringLiteral("package manifest JSON must be an object"), span);
        return false;
    }
    object = doc.object();
    return true;
}

bool writeManifestObject(const QString& manifestFile,
                         const QJsonObject& object,
                         QList<Diagnostic>& diagnostics)
{
    QFile file(manifestFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        SourceSpan span;
        span.file = manifestFile;
        addPackageError(diagnostics, QStringLiteral("cannot write package manifest '%1'").arg(manifestFile), span);
        return false;
    }
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size()) {
        SourceSpan span;
        span.file = manifestFile;
        addPackageError(diagnostics, QStringLiteral("failed to write package manifest '%1'").arg(manifestFile), span);
        return false;
    }
    return true;
}

void copyLockResultIntoChange(PackageDependencyChangeResult& result, const PackageLockResult& lock)
{
    result.lockFile = lock.lockFile;
    result.lockedPackages = lock.entries.size();
    result.diagnostics.append(lock.diagnostics);
}

} // namespace

QString PackageManifest::entryFilePath() const
{
    QFileInfo info(entry);
    if (info.isRelative() && !rootDir.isEmpty())
        info = QFileInfo(QDir(rootDir).absoluteFilePath(entry));
    return info.absoluteFilePath();
}

QString packageManifestFileName()
{
    return QString::fromLatin1(kPackageManifestFileName);
}

QString packageLockFileName()
{
    return QString::fromLatin1(kPackageLockFileName);
}

bool isPackageDirectory(const QString& path)
{
    const QFileInfo info(path);
    if (!info.isDir())
        return false;
    return QFileInfo(QDir(info.absoluteFilePath()).absoluteFilePath(packageManifestFileName())).isFile();
}

PackageInitResult initPackageProject(const PackageInitOptions& options)
{
    PackageInitResult result;
    const QString rootPath = options.rootDir.trimmed().isEmpty() ? QDir::currentPath() : options.rootDir;
    QFileInfo rootInfo(rootPath);
    if (rootInfo.exists() && !rootInfo.isDir()) {
        SourceSpan span;
        span.file = rootPath;
        addPackageError(result.diagnostics, QStringLiteral("package init target '%1' is not a directory").arg(rootPath), span);
        return result;
    }

    QDir rootDir(rootInfo.absoluteFilePath());
    if (!rootInfo.exists()) {
        if (!QDir().mkpath(rootInfo.absoluteFilePath())) {
            SourceSpan span;
            span.file = rootInfo.absoluteFilePath();
            addPackageError(result.diagnostics, QStringLiteral("cannot create directory '%1'").arg(rootInfo.absoluteFilePath()), span);
            return result;
        }
        rootDir = QDir(rootInfo.absoluteFilePath());
    }
    result.rootDir = rootDir.absolutePath();

    if (!rootDir.mkpath(QStringLiteral("src")) || !rootDir.mkpath(QStringLiteral("examples"))) {
        SourceSpan span;
        span.file = result.rootDir;
        addPackageError(result.diagnostics, QStringLiteral("cannot create package subdirectories under '%1'").arg(result.rootDir), span);
        return result;
    }

    const QString packageName = options.name.trimmed().isEmpty() ? defaultPackageNameForDir(result.rootDir) : options.name.trimmed();
    const QString packageVersion = options.version.trimmed().isEmpty() ? QStringLiteral("0.1.0") : options.version.trimmed();
    QJsonObject manifest;
    manifest.insert(QStringLiteral("name"), packageName);
    manifest.insert(QStringLiteral("version"), packageVersion);
    manifest.insert(QStringLiteral("entry"), QStringLiteral("src/main.abel"));
    const QString manifestText = QString::fromUtf8(QJsonDocument(manifest).toJson(QJsonDocument::Indented));

    const QString mainText = QStringLiteral(
        "fn int main() {\n"
        "    println(\"hello from Abel\");\n"
        "    return 0;\n"
        "}\n");
    const QString readmeText = QStringLiteral(
        "# %1\n\n"
        "Generated by `abel init`.\n\n"
        "## Run\n\n"
        "```bash\n"
        "abel package check .\n"
        "abel check .\n"
        "abel run .\n"
        "```\n").arg(packageName);
    const QString gitignoreText = QStringLiteral(
        "build/\n"
        ".abel/\n"
        "*.tmp\n");

    writeNewTextFile(rootDir.absoluteFilePath(packageManifestFileName()), manifestText, result.createdFiles, result.diagnostics);
    writeNewTextFile(rootDir.absoluteFilePath(QStringLiteral("src/main.abel")), mainText, result.createdFiles, result.diagnostics);
    writeNewTextFile(rootDir.absoluteFilePath(QStringLiteral("README.md")), readmeText, result.createdFiles, result.diagnostics);
    writeNewTextFile(rootDir.absoluteFilePath(QStringLiteral(".gitignore")), gitignoreText, result.createdFiles, result.diagnostics);

    return result;
}

PackageLockResult resolvePackageLock(const QString& dir)
{
    PackageLockResult result;
    auto parsed = packageManifestFromDirectory(dir);
    result.diagnostics.append(parsed.diagnostics);
    if (!parsed.ok())
        return result;

    result.rootDir = parsed.package.rootDir;
    result.rootName = parsed.package.name;
    result.rootVersion = parsed.package.version;
    result.lockFile = QDir(result.rootDir).absoluteFilePath(packageLockFileName());

    QSet<QString> seen;
    QSet<QString> resolving;
    resolving.insert(absolutePathFrom(QString(), result.rootDir));
    resolvePackageDependencies(parsed.package, result, seen, resolving);
    return result;
}

PackageLockResult updatePackageLock(const QString& dir)
{
    PackageLockResult result = resolvePackageLock(dir);
    if (!result.ok())
        return result;

    QFile file(result.lockFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        SourceSpan span;
        span.file = result.lockFile;
        addPackageError(result.diagnostics, QStringLiteral("cannot write lockfile '%1'").arg(result.lockFile), span);
        return result;
    }
    const QByteArray bytes = packageLockToJson(result).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size()) {
        SourceSpan span;
        span.file = result.lockFile;
        addPackageError(result.diagnostics, QStringLiteral("failed to write lockfile '%1'").arg(result.lockFile), span);
    }
    return result;
}

PackageDependencyChangeResult addPathPackageDependency(const QString& dir, const QString& dependencyDir)
{
    PackageDependencyChangeResult result;
    QJsonObject manifestObject;
    if (!readManifestObjectForUpdate(dir, manifestObject, result.manifestFile, result.rootDir, result.diagnostics))
        return result;

    auto rootPackage = packageManifestFromJson(manifestObject, result.rootDir, SourceSpan{result.manifestFile});
    result.diagnostics.append(rootPackage.diagnostics);
    if (!rootPackage.ok())
        return result;

    auto dependencyPackage = packageManifestFromDirectory(dependencyDir);
    result.diagnostics.append(dependencyPackage.diagnostics);
    if (!dependencyPackage.ok())
        return result;

    PackageDependency dependency;
    dependency.name = dependencyPackage.package.name;
    dependency.kind = QStringLiteral("path");
    dependency.path = QDir(result.rootDir).relativeFilePath(dependencyPackage.package.rootDir);
    dependency.version = dependencyPackage.package.version;
    result.dependency = dependency;

    QJsonArray dependencies;
    const QJsonValue existingValue = manifestObject.value(QStringLiteral("dependencies"));
    if (!existingValue.isUndefined()) {
        if (!existingValue.isArray()) {
            SourceSpan span;
            span.file = result.manifestFile;
            addPackageError(result.diagnostics, QStringLiteral("package manifest field 'dependencies' must be an array"), span);
            return result;
        }
        dependencies = existingValue.toArray();
    }

    bool replaced = false;
    for (qsizetype i = 0; i < dependencies.size(); ++i) {
        const QJsonValue raw = dependencies.at(i);
        if (!raw.isObject())
            continue;
        const QJsonObject object = raw.toObject();
        if (object.value(QStringLiteral("name")).toString() != dependency.name)
            continue;
        if (object == dependencyToJson(dependency)) {
            replaced = true;
            break;
        }
        dependencies.replace(i, dependencyToJson(dependency));
        result.changed = true;
        replaced = true;
        break;
    }
    if (!replaced) {
        dependencies.push_back(dependencyToJson(dependency));
        result.changed = true;
    }

    if (result.changed) {
        manifestObject.insert(QStringLiteral("dependencies"), dependencies);
        if (!writeManifestObject(result.manifestFile, manifestObject, result.diagnostics))
            return result;
    }

    const auto lock = updatePackageLock(result.rootDir);
    copyLockResultIntoChange(result, lock);
    return result;
}

PackageDependencyChangeResult removePackageDependency(const QString& dir, const QString& dependencyName)
{
    PackageDependencyChangeResult result;
    QJsonObject manifestObject;
    if (!readManifestObjectForUpdate(dir, manifestObject, result.manifestFile, result.rootDir, result.diagnostics))
        return result;

    const QString name = dependencyName.trimmed();
    if (name.isEmpty()) {
        SourceSpan span;
        span.file = result.manifestFile;
        addPackageError(result.diagnostics, QStringLiteral("dependency name must not be empty"), span);
        return result;
    }

    QJsonArray dependencies;
    const QJsonValue existingValue = manifestObject.value(QStringLiteral("dependencies"));
    if (!existingValue.isUndefined()) {
        if (!existingValue.isArray()) {
            SourceSpan span;
            span.file = result.manifestFile;
            addPackageError(result.diagnostics, QStringLiteral("package manifest field 'dependencies' must be an array"), span);
            return result;
        }
        dependencies = existingValue.toArray();
    }

    QJsonArray kept;
    bool removed = false;
    for (const QJsonValue& raw : dependencies) {
        if (raw.isObject() && raw.toObject().value(QStringLiteral("name")).toString() == name) {
            const QJsonObject object = raw.toObject();
            result.dependency.name = object.value(QStringLiteral("name")).toString();
            result.dependency.kind = object.value(QStringLiteral("kind")).toString();
            result.dependency.path = object.value(QStringLiteral("path")).toString();
            result.dependency.version = object.value(QStringLiteral("version")).toString();
            removed = true;
            continue;
        }
        kept.push_back(raw);
    }

    if (!removed) {
        SourceSpan span;
        span.file = result.manifestFile;
        addPackageError(result.diagnostics, QStringLiteral("dependency '%1' is not declared").arg(name), span);
        return result;
    }

    result.changed = true;
    if (kept.isEmpty())
        manifestObject.remove(QStringLiteral("dependencies"));
    else
        manifestObject.insert(QStringLiteral("dependencies"), kept);
    if (!writeManifestObject(result.manifestFile, manifestObject, result.diagnostics))
        return result;

    const auto lock = updatePackageLock(result.rootDir);
    copyLockResultIntoChange(result, lock);
    return result;
}

PackageManifestParseResult packageManifestFromJson(const QJsonObject& object,
                                                   const QString& rootDir,
                                                   const SourceSpan& span)
{
    PackageManifestParseResult result;
    result.package.rootDir = rootDir.isEmpty() ? QDir::currentPath() : QFileInfo(rootDir).absoluteFilePath();
    result.package.name = requiredString(object, QStringLiteral("name"), result.diagnostics, span);
    result.package.version = requiredString(object, QStringLiteral("version"), result.diagnostics, span);
    result.package.entry = requiredString(object, QStringLiteral("entry"), result.diagnostics, span);

    const QJsonValue backendArtifacts = object.value(QStringLiteral("backendArtifacts"));
    if (!backendArtifacts.isUndefined()) {
        if (!backendArtifacts.isArray()) {
            addPackageError(result.diagnostics, QStringLiteral("package manifest field 'backendArtifacts' must be an array"), span);
        } else {
            for (const QJsonValue& artifact : backendArtifacts.toArray()) {
                if (!artifact.isObject()) {
                    addPackageError(result.diagnostics, QStringLiteral("backend artifact must be an object"), span);
                    continue;
                }
                result.package.backendArtifacts.push_back(parseBackendArtifact(artifact.toObject(), result.diagnostics, span));
            }
        }
    }

    if (!result.package.entry.isEmpty() && !result.package.rootDir.isEmpty()) {
        const QFileInfo entryInfo(result.package.entryFilePath());
        if (!entryInfo.isFile())
            addPackageError(result.diagnostics,
                            QStringLiteral("package entry '%1' does not exist").arg(result.package.entry),
                            span);
    }

    const QJsonValue dependencies = object.value(QStringLiteral("dependencies"));
    if (!dependencies.isUndefined()) {
        if (!dependencies.isArray()) {
            addPackageError(result.diagnostics, QStringLiteral("package manifest field 'dependencies' must be an array"), span);
        } else {
            for (const QJsonValue& dependency : dependencies.toArray()) {
                if (!dependency.isObject()) {
                    addPackageError(result.diagnostics, QStringLiteral("dependency must be an object"), span);
                    continue;
                }
                result.package.dependencies.push_back(parseDependency(dependency.toObject(), result.diagnostics, span));
            }
        }
    }

    return result;
}

PackageManifestParseResult packageManifestFromJsonText(const QString& text,
                                                       const QString& file,
                                                       const QString& rootDir)
{
    SourceSpan span;
    span.file = file;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        PackageManifestParseResult result;
        addPackageError(result.diagnostics, QStringLiteral("invalid package manifest JSON: %1").arg(parseError.errorString()), span);
        return result;
    }
    if (!doc.isObject()) {
        PackageManifestParseResult result;
        addPackageError(result.diagnostics, QStringLiteral("package manifest JSON must be an object"), span);
        return result;
    }
    auto result = packageManifestFromJson(doc.object(), rootDir, span);
    result.package.filePath = file;
    return result;
}

PackageManifestParseResult packageManifestFromFile(const QString& manifestFile)
{
    QFile file(manifestFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        PackageManifestParseResult result;
        SourceSpan span;
        span.file = manifestFile;
        addPackageError(result.diagnostics, QStringLiteral("cannot open package manifest '%1'").arg(manifestFile), span);
        return result;
    }
    const QFileInfo info(manifestFile);
    auto result = packageManifestFromJsonText(QString::fromUtf8(file.readAll()),
                                              info.absoluteFilePath(),
                                              info.absoluteDir().absolutePath());
    result.package.filePath = info.absoluteFilePath();
    return result;
}

PackageManifestParseResult packageManifestFromDirectory(const QString& dir)
{
    const QFileInfo info(dir);
    if (!info.isDir()) {
        PackageManifestParseResult result;
        SourceSpan span;
        span.file = dir;
        addPackageError(result.diagnostics, QStringLiteral("package path '%1' is not a directory").arg(dir), span);
        return result;
    }
    const QString manifest = QDir(info.absoluteFilePath()).absoluteFilePath(packageManifestFileName());
    return packageManifestFromFile(manifest);
}

} // namespace abel
