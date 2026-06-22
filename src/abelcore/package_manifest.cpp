#include "abelcore/package_manifest.h"

#include "abelcore/backend_interface.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QTextStream>
#include <QtGlobal>

namespace abel {

namespace {

constexpr auto kPackageManifestFileName = "abel.package.json";

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
