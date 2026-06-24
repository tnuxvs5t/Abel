#include "abelcore/package_manifest.h"

#include "abelcore/backend_interface.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QProcess>
#include <QSet>
#include <QTextStream>
#include <QUrl>
#include <QtGlobal>

#include <algorithm>
#include <optional>

namespace abel {

namespace {

constexpr auto kPackageManifestFileName = "abel.package.json";
constexpr auto kPackageLockFileName = "abel.lock.json";
constexpr auto kPackageLocalRegistryIndexFileName = ".abel-registry.json";
constexpr auto kPackageCacheDirName = ".abel";
constexpr auto kPackageCacheBackendDir = "cache/backend";
constexpr auto kPackageCachePackagesDir = "cache/packages";
constexpr auto kPackageCacheRegistriesDir = "cache/registries";

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

QString truncatedProcessText(QString text)
{
    constexpr qsizetype maxChars = 4000;
    text = text.trimmed();
    if (text.size() <= maxChars)
        return text;
    return text.left(maxChars) + QStringLiteral("\n<truncated>");
}

QString sanitizeCacheSegment(QString value)
{
    value = value.trimmed();
    if (value.isEmpty())
        return QStringLiteral("_");
    QString out;
    out.reserve(value.size());
    for (const QChar ch : value) {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('.') || ch == QLatin1Char('_') || ch == QLatin1Char('-'))
            out.push_back(ch);
        else
            out.push_back(QLatin1Char('_'));
    }
    return out.isEmpty() ? QStringLiteral("_") : out;
}

bool copyDirectoryRecursively(const QString& sourceDir,
                              const QString& targetDir,
                              QList<Diagnostic>& diagnostics,
                              const SourceSpan& span);

QString backendArtifactSourcePath(const PackageResolvedResource& resource)
{
    return absolutePathFrom(resource.packageRoot, resource.node.path);
}

QString backendArtifactCachedPath(const QString& rootDir, const PackageResolvedResource& resource)
{
    QString fileName = QFileInfo(resource.node.path).fileName();
    if (fileName.isEmpty())
        fileName = QFileInfo(backendArtifactSourcePath(resource)).fileName();
    if (fileName.isEmpty())
        fileName = QStringLiteral("backend");

    QDir cacheDir(packageBackendCacheDir(rootDir));
    const QString packagePart = sanitizeCacheSegment(resource.packageName);
    const QString backendPart = sanitizeCacheSegment(resource.node.backendId);
    return cacheDir.absoluteFilePath(packagePart + QStringLiteral("/")
                                     + backendPart + QStringLiteral("/")
                                     + fileName);
}

QString registryPackageCachePath(const QString& rootDir, const QString& name, const QString& version)
{
    QDir cacheDir(packageRegistryCacheDir(rootDir));
    return cacheDir.absoluteFilePath(sanitizeCacheSegment(name) + QStringLiteral("/")
                                     + sanitizeCacheSegment(version));
}

QString registryMirrorCachePath(const QString& rootDir, const QString& registry)
{
    QDir cacheDir(packageRegistryMirrorCacheDir(rootDir));
    return cacheDir.absoluteFilePath(sanitizeCacheSegment(registry));
}

QString backendArtifactCacheMetadataPath(const QString& cachedPath)
{
    return cachedPath + QStringLiteral(".abel-cache.json");
}

QString canonicalOrAbsoluteFilePath(const QFileInfo& info)
{
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

QString localRegistryRootPath(const QString& registryDir)
{
    const QString registryInput = registryDir.trimmed().isEmpty() ? QStringLiteral(".") : registryDir;
    QFileInfo registryInfo(registryInput);
    if (!registryInfo.isAbsolute())
        registryInfo = QFileInfo(QDir::current().absoluteFilePath(registryInput));
    const QString canonical = registryInfo.canonicalFilePath();
    return registryInfo.exists() && !canonical.isEmpty()
        ? canonical
        : registryInfo.absoluteFilePath();
}

std::optional<QString> localSourcePathForRegistryUri(const QString& registry,
                                                     const QString& packageRoot,
                                                     QList<Diagnostic>& diagnostics,
                                                     const SourceSpan& span)
{
    const QString trimmed = registry.trimmed();
    const QUrl url(trimmed);
    if (!url.isValid() || url.scheme() != QStringLiteral("file")) {
        addPackageError(diagnostics,
                        QStringLiteral("registry URI '%1' is not supported yet; only file:// registry mirrors are implemented")
                            .arg(registry),
                        span);
        return std::nullopt;
    }
    QString path = url.toLocalFile();
    if (path.isEmpty()) {
        addPackageError(diagnostics,
                        QStringLiteral("file registry URI '%1' does not contain a local path").arg(registry),
                        span);
        return std::nullopt;
    }
    return absolutePathFrom(packageRoot, path);
}

std::optional<QString> registryResolutionRoot(const QString& registry,
                                              const QString& packageRoot,
                                              const QString& projectRoot,
                                              QList<Diagnostic>& diagnostics,
                                              const SourceSpan& span)
{
    if (!registry.contains(QStringLiteral("://")))
        return absolutePathFrom(packageRoot, registry);

    const auto sourcePath = localSourcePathForRegistryUri(registry, packageRoot, diagnostics, span);
    if (!sourcePath.has_value())
        return std::nullopt;

    const QFileInfo sourceInfo(*sourcePath);
    if (!sourceInfo.isDir()) {
        addPackageError(diagnostics,
                        QStringLiteral("registry URI '%1' points to missing directory '%2'")
                            .arg(registry, *sourcePath),
                        span);
        return std::nullopt;
    }

    const QString mirrorPath = registryMirrorCachePath(projectRoot, registry);
    if (!copyDirectoryRecursively(*sourcePath, mirrorPath, diagnostics, span))
        return std::nullopt;
    return mirrorPath;
}

QString fileSizeStamp(const QFileInfo& info)
{
    return QString::number(info.size());
}

QString fileMTimeStamp(const QFileInfo& info)
{
    return QString::number(info.lastModified().toMSecsSinceEpoch());
}

QJsonArray symbolsToJson(const QStringList& symbols)
{
    QJsonArray out;
    for (const QString& symbol : symbols)
        out.push_back(symbol);
    return out;
}

bool symbolsMatchJson(const QJsonValue& value, const QStringList& expected)
{
    if (!value.isArray())
        return false;
    const QJsonArray array = value.toArray();
    if (array.size() != expected.size())
        return false;
    for (qsizetype i = 0; i < array.size(); ++i) {
        if (!array.at(i).isString() || array.at(i).toString() != expected.at(i))
            return false;
    }
    return true;
}

QJsonObject backendArtifactCacheMetadata(const PackageResolvedResource& resource,
                                         const QString& sourcePath,
                                         const QFileInfo& sourceInfo)
{
    QJsonObject object;
    object.insert(QStringLiteral("formatVersion"), 2);
    object.insert(QStringLiteral("packageName"), resource.packageName);
    object.insert(QStringLiteral("backendId"), resource.node.backendId);
    object.insert(QStringLiteral("sourcePath"), canonicalOrAbsoluteFilePath(sourceInfo));
    object.insert(QStringLiteral("sourceSize"), fileSizeStamp(sourceInfo));
    object.insert(QStringLiteral("sourceMTimeMs"), fileMTimeStamp(sourceInfo));
    object.insert(QStringLiteral("kind"), resource.node.kind);
    object.insert(QStringLiteral("iid"), resource.node.iid);
    object.insert(QStringLiteral("qtVersion"), resource.node.qtVersion);
    object.insert(QStringLiteral("kit"), resource.node.kit);
    object.insert(QStringLiteral("platform"), resource.node.platform);
    object.insert(QStringLiteral("compiler"), resource.node.compiler);
    object.insert(QStringLiteral("compilerVersion"), resource.node.compilerVersion);
    object.insert(QStringLiteral("cxxStandard"), resource.node.cxxStandard);
    object.insert(QStringLiteral("abelAbi"), resource.node.abelAbi);
    object.insert(QStringLiteral("symbols"), symbolsToJson(resource.node.symbols));
    object.insert(QStringLiteral("declaredPath"), resource.node.path);
    object.insert(QStringLiteral("sourcePathInput"), sourcePath);
    return object;
}

bool writeBackendArtifactCacheMetadata(const QString& metadataPath,
                                       const PackageResolvedResource& resource,
                                       const QString& sourcePath,
                                       const QFileInfo& sourceInfo,
                                       QList<Diagnostic>& diagnostics)
{
    QFile file(metadataPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        SourceSpan span;
        span.file = metadataPath;
        addPackageError(diagnostics,
                        QStringLiteral("cannot write backend artifact cache metadata '%1'").arg(metadataPath),
                        span);
        return false;
    }
    const QByteArray bytes = QJsonDocument(backendArtifactCacheMetadata(resource, sourcePath, sourceInfo))
                                 .toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size()) {
        SourceSpan span;
        span.file = metadataPath;
        addPackageError(diagnostics,
                        QStringLiteral("failed to write backend artifact cache metadata '%1'").arg(metadataPath),
                        span);
        return false;
    }
    return true;
}

bool backendArtifactCacheMetadataMatches(const QString& metadataPath,
                                         const PackageResolvedResource& resource,
                                         const QString& sourcePath)
{
    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.isFile())
        return false;

    QFile file(metadataPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    const QJsonObject object = doc.object();
    return object.value(QStringLiteral("formatVersion")).toInt() == 2
        && object.value(QStringLiteral("packageName")).toString() == resource.packageName
        && object.value(QStringLiteral("backendId")).toString() == resource.node.backendId
        && object.value(QStringLiteral("sourcePath")).toString() == canonicalOrAbsoluteFilePath(sourceInfo)
        && object.value(QStringLiteral("sourceSize")).toString() == fileSizeStamp(sourceInfo)
        && object.value(QStringLiteral("sourceMTimeMs")).toString() == fileMTimeStamp(sourceInfo)
        && object.value(QStringLiteral("kind")).toString() == resource.node.kind
        && object.value(QStringLiteral("iid")).toString() == resource.node.iid
        && object.value(QStringLiteral("qtVersion")).toString() == resource.node.qtVersion
        && object.value(QStringLiteral("kit")).toString() == resource.node.kit
        && object.value(QStringLiteral("platform")).toString() == resource.node.platform
        && object.value(QStringLiteral("compiler")).toString() == resource.node.compiler
        && object.value(QStringLiteral("compilerVersion")).toString() == resource.node.compilerVersion
        && object.value(QStringLiteral("cxxStandard")).toString() == resource.node.cxxStandard
        && object.value(QStringLiteral("abelAbi")).toString() == resource.node.abelAbi
        && symbolsMatchJson(object.value(QStringLiteral("symbols")), resource.node.symbols)
        && object.value(QStringLiteral("declaredPath")).toString() == resource.node.path
        && object.value(QStringLiteral("sourcePathInput")).toString() == sourcePath;
}

QStringList parseStringList(const QJsonObject& object,
                            const QString& key,
                            QList<Diagnostic>& diagnostics,
                            const SourceSpan& span)
{
    QStringList out;
    const QJsonValue value = object.value(key);
    if (value.isUndefined())
        return out;
    if (!value.isArray()) {
        addPackageError(diagnostics, QStringLiteral("field '%1' must be an array of strings").arg(key), span);
        return out;
    }
    for (const QJsonValue& raw : value.toArray()) {
        if (!raw.isString()) {
            addPackageError(diagnostics, QStringLiteral("field '%1' must contain only strings").arg(key), span);
            continue;
        }
        out.push_back(raw.toString());
    }
    return out;
}

PackageBackendBuildSpec parseBackendBuildSpec(const QJsonValue& value,
                                              QList<Diagnostic>& diagnostics,
                                              const SourceSpan& span)
{
    PackageBackendBuildSpec build;
    if (value.isUndefined())
        return build;
    build.enabled = true;
    if (!value.isObject()) {
        addPackageError(diagnostics, QStringLiteral("backend artifact field 'build' must be an object"), span);
        return build;
    }

    const QJsonObject object = value.toObject();
    build.system = optionalString(object, QStringLiteral("system"), QStringLiteral("cmake"));
    if (build.system != QStringLiteral("cmake")) {
        addPackageError(diagnostics,
                        QStringLiteral("backend artifact build system '%1' is not supported yet; only 'cmake' is supported")
                            .arg(build.system),
                        span);
        return build;
    }
    build.cmake = optionalString(object, QStringLiteral("cmake"), QString());
    build.source = requiredString(object, QStringLiteral("source"), diagnostics, span);
    build.buildDir = requiredString(object, QStringLiteral("buildDir"), diagnostics, span);
    build.generator = optionalString(object, QStringLiteral("generator"), QString());
    build.target = optionalString(object, QStringLiteral("target"), QString());
    build.configureArgs = parseStringList(object, QStringLiteral("configureArgs"), diagnostics, span);
    build.buildArgs = parseStringList(object, QStringLiteral("buildArgs"), diagnostics, span);
    return build;
}

QString cmakeExecutable(const PackageBackendBuildSpec& build)
{
    if (!build.cmake.trimmed().isEmpty())
        return build.cmake.trimmed();
    const QString env = qEnvironmentVariable("CMAKE");
    if (!env.trimmed().isEmpty())
        return env.trimmed();
    return QStringLiteral("cmake");
}

bool runPackageProcess(const QString& program,
                       const QStringList& args,
                       const QString& workingDir,
                       const QString& label,
                       const SourceSpan& span,
                       QList<Diagnostic>& diagnostics)
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(args);
    if (!workingDir.isEmpty())
        process.setWorkingDirectory(workingDir);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start();
    if (!process.waitForStarted()) {
        addPackageError(diagnostics,
                        QStringLiteral("%1 failed to start '%2': %3")
                            .arg(label, program, process.errorString()),
                        span);
        return false;
    }
    if (!process.waitForFinished(300000)) {
        process.kill();
        process.waitForFinished();
        addPackageError(diagnostics,
                        QStringLiteral("%1 timed out while running '%2'").arg(label, program),
                        span);
        return false;
    }
    const QString output = QString::fromUtf8(process.readAllStandardOutput());
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        addPackageError(diagnostics,
                        QStringLiteral("%1 failed with exit code %2 while running '%3 %4'%5%6")
                            .arg(label)
                            .arg(process.exitCode())
                            .arg(program,
                                 args.join(QLatin1Char(' ')),
                                 output.trimmed().isEmpty() ? QString() : QStringLiteral(":\n"),
                                 truncatedProcessText(output)),
                        span);
        return false;
    }
    return true;
}

bool buildBackendArtifact(const PackageResolvedResource& resource,
                          QList<Diagnostic>& diagnostics)
{
    if (!resource.build.enabled)
        return true;

    SourceSpan span;
    span.file = QDir(resource.packageRoot).absoluteFilePath(packageManifestFileName());
    const QString sourceDir = absolutePathFrom(resource.packageRoot, resource.build.source);
    const QString buildDir = absolutePathFrom(resource.packageRoot, resource.build.buildDir);
    if (!QFileInfo(sourceDir).isDir()) {
        addPackageError(diagnostics,
                        QStringLiteral("backend artifact build source directory '%1' from package '%2' does not exist")
                            .arg(resource.build.source, resource.packageName),
                        span);
        return false;
    }
    if (!QDir().mkpath(buildDir)) {
        addPackageError(diagnostics,
                        QStringLiteral("cannot create backend artifact build directory '%1'").arg(buildDir),
                        span);
        return false;
    }

    QStringList configureArgs;
    configureArgs << QStringLiteral("-S") << sourceDir
                  << QStringLiteral("-B") << buildDir;
    if (!resource.build.generator.isEmpty())
        configureArgs << QStringLiteral("-G") << resource.build.generator;
    configureArgs << resource.build.configureArgs;

    const QString cmake = cmakeExecutable(resource.build);
    if (!runPackageProcess(cmake,
                           configureArgs,
                           resource.packageRoot,
                           QStringLiteral("configure backend artifact '%1' from package '%2'")
                               .arg(resource.node.backendId, resource.packageName),
                           span,
                           diagnostics)) {
        return false;
    }

    QStringList buildArgs;
    buildArgs << QStringLiteral("--build") << buildDir;
    if (!resource.build.target.isEmpty())
        buildArgs << QStringLiteral("--target") << resource.build.target;
    buildArgs << resource.build.buildArgs;

    return runPackageProcess(cmake,
                             buildArgs,
                             resource.packageRoot,
                             QStringLiteral("build backend artifact '%1' from package '%2'")
                                 .arg(resource.node.backendId, resource.packageName),
                             span,
                             diagnostics);
}

struct SemVer {
    int major = 0;
    int minor = 0;
    int patch = 0;
};

bool parseVersionComponent(const QString& text, int& out)
{
    if (text.isEmpty())
        return false;
    for (const QChar ch : text) {
        if (!ch.isDigit())
            return false;
    }
    bool ok = false;
    const int value = text.toInt(&ok);
    if (!ok)
        return false;
    out = value;
    return true;
}

bool parseSemVerCore(const QString& text, SemVer& out, int* partsOut = nullptr, bool allowPartial = false)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || trimmed.contains(QLatin1Char('-')) || trimmed.contains(QLatin1Char('+')))
        return false;
    const QStringList parts = trimmed.split(QLatin1Char('.'));
    if (parts.isEmpty() || parts.size() > 3 || (!allowPartial && parts.size() != 3))
        return false;

    int values[3] = {0, 0, 0};
    for (qsizetype i = 0; i < parts.size(); ++i) {
        if (!parseVersionComponent(parts.at(i), values[i]))
            return false;
    }
    out.major = values[0];
    out.minor = values[1];
    out.patch = values[2];
    if (partsOut)
        *partsOut = static_cast<int>(parts.size());
    return true;
}

int compareSemVer(const SemVer& a, const SemVer& b)
{
    if (a.major != b.major)
        return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor)
        return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch)
        return a.patch < b.patch ? -1 : 1;
    return 0;
}

QString normalizedVersionRequirement(QString requirement)
{
    requirement = requirement.trimmed();
    for (qsizetype i = 0; i < requirement.size(); ++i) {
        if (requirement[i].isSpace() || requirement[i] == QLatin1Char(','))
            requirement[i] = QLatin1Char(' ');
    }
    return requirement;
}

QStringList versionRequirementTokens(const QString& requirement)
{
    const QString normalized = normalizedVersionRequirement(requirement);
    if (normalized.isEmpty())
        return {};
    return normalized.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

bool parseRequirementVersion(const QString& text, SemVer& out, int* partsOut, QString* error)
{
    if (!parseSemVerCore(text, out, partsOut, true)) {
        if (error)
            *error = QStringLiteral("invalid SemVer requirement component '%1'").arg(text);
        return false;
    }
    return true;
}

bool validateVersionRequirementSyntax(const QString& requirement, QString* error = nullptr)
{
    const QString trimmed = requirement.trimmed();
    if (trimmed.isEmpty() || trimmed == QStringLiteral("*"))
        return true;

    const QStringList tokens = versionRequirementTokens(trimmed);
    if (tokens.isEmpty())
        return true;

    for (const QString& token : tokens) {
        if (token == QStringLiteral("*"))
            continue;

        QString versionText = token;
        if (versionText.startsWith(QStringLiteral(">=")) || versionText.startsWith(QStringLiteral("<=")))
            versionText = versionText.mid(2);
        else if (versionText.startsWith(QLatin1Char('>'))
                 || versionText.startsWith(QLatin1Char('<'))
                 || versionText.startsWith(QLatin1Char('='))
                 || versionText.startsWith(QLatin1Char('^'))
                 || versionText.startsWith(QLatin1Char('~'))) {
            versionText = versionText.mid(1);
        }

        SemVer ignored;
        int ignoredParts = 0;
        if (!parseRequirementVersion(versionText, ignored, &ignoredParts, error))
            return false;
    }
    return true;
}

bool satisfiesComparator(const SemVer& actual, const QString& op, const SemVer& expected)
{
    const int cmp = compareSemVer(actual, expected);
    if (op == QStringLiteral(">"))
        return cmp > 0;
    if (op == QStringLiteral(">="))
        return cmp >= 0;
    if (op == QStringLiteral("<"))
        return cmp < 0;
    if (op == QStringLiteral("<="))
        return cmp <= 0;
    return cmp == 0;
}

SemVer caretUpperBound(const SemVer& lower)
{
    SemVer upper;
    if (lower.major > 0) {
        upper.major = lower.major + 1;
        return upper;
    }
    if (lower.minor > 0) {
        upper.minor = lower.minor + 1;
        return upper;
    }
    upper.patch = lower.patch + 1;
    return upper;
}

SemVer tildeUpperBound(const SemVer& lower, int parts)
{
    SemVer upper;
    if (parts <= 1) {
        upper.major = lower.major + 1;
        return upper;
    }
    upper.major = lower.major;
    upper.minor = lower.minor + 1;
    return upper;
}

bool versionSatisfiesRequirement(const QString& version, const QString& requirement, QString* error = nullptr)
{
    if (error)
        error->clear();

    SemVer actual;
    if (!parseSemVerCore(version, actual)) {
        if (error)
            *error = QStringLiteral("package version '%1' is not supported; expected SemVer major.minor.patch").arg(version);
        return false;
    }

    const QString trimmed = requirement.trimmed();
    if (trimmed.isEmpty() || trimmed == QStringLiteral("*"))
        return true;

    QString syntaxError;
    if (!validateVersionRequirementSyntax(trimmed, &syntaxError)) {
        if (error)
            *error = syntaxError;
        return false;
    }

    for (QString token : versionRequirementTokens(trimmed)) {
        if (token == QStringLiteral("*"))
            continue;

        if (token.startsWith(QLatin1Char('^'))) {
            SemVer lower;
            int parts = 0;
            parseRequirementVersion(token.mid(1), lower, &parts, nullptr);
            const SemVer upper = caretUpperBound(lower);
            if (compareSemVer(actual, lower) < 0 || compareSemVer(actual, upper) >= 0)
                return false;
            continue;
        }

        if (token.startsWith(QLatin1Char('~'))) {
            SemVer lower;
            int parts = 0;
            parseRequirementVersion(token.mid(1), lower, &parts, nullptr);
            const SemVer upper = tildeUpperBound(lower, parts);
            if (compareSemVer(actual, lower) < 0 || compareSemVer(actual, upper) >= 0)
                return false;
            continue;
        }

        QString op = QStringLiteral("=");
        QString versionText = token;
        if (token.startsWith(QStringLiteral(">=")) || token.startsWith(QStringLiteral("<="))) {
            op = token.left(2);
            versionText = token.mid(2);
        } else if (token.startsWith(QLatin1Char('>')) || token.startsWith(QLatin1Char('<')) || token.startsWith(QLatin1Char('='))) {
            op = token.left(1);
            versionText = token.mid(1);
        }

        SemVer expected;
        int parts = 0;
        parseRequirementVersion(versionText, expected, &parts, nullptr);
        if (!satisfiesComparator(actual, op, expected))
            return false;
    }
    return true;
}

PackageDependency parseDependency(const QJsonObject& object,
                                  QList<Diagnostic>& diagnostics,
                                  const SourceSpan& span)
{
    PackageDependency dependency;
    dependency.name = requiredString(object, QStringLiteral("name"), diagnostics, span);
    dependency.kind = requiredString(object, QStringLiteral("kind"), diagnostics, span);
    dependency.version = optionalString(object, QStringLiteral("version"), QString());
    QString versionRequirementError;
    if (!validateVersionRequirementSyntax(dependency.version, &versionRequirementError)) {
        addPackageError(diagnostics,
                        QStringLiteral("dependency '%1' has invalid version requirement '%2': %3")
                            .arg(dependency.name, dependency.version, versionRequirementError),
                        span);
    }
    if (dependency.kind == QStringLiteral("path")) {
        dependency.path = requiredString(object, QStringLiteral("path"), diagnostics, span);
        return dependency;
    }
    if (dependency.kind == QStringLiteral("registry")) {
        dependency.registry = requiredString(object, QStringLiteral("registry"), diagnostics, span);
        return dependency;
    }
    {
        addPackageError(diagnostics,
                        QStringLiteral("dependency '%1' kind '%2' is not supported yet; expected 'path' or 'registry'")
                            .arg(dependency.name, dependency.kind),
                        span);
        return dependency;
    }
}

bool copyDirectoryRecursively(const QString& sourceDir, const QString& targetDir, QList<Diagnostic>& diagnostics, const SourceSpan& span)
{
    const QFileInfo sourceInfo(sourceDir);
    const QFileInfo targetInfo(targetDir);
    const QString sourceRoot = canonicalOrAbsoluteFilePath(sourceInfo);
    const QString targetRoot = targetInfo.exists() ? canonicalOrAbsoluteFilePath(targetInfo) : targetInfo.absoluteFilePath();
    if (sourceRoot == targetRoot)
        return true;
    if (QFileInfo::exists(targetDir) && !QDir(targetDir).removeRecursively()) {
        addPackageError(diagnostics, QStringLiteral("cannot refresh package directory '%1'").arg(targetDir), span);
        return false;
    }
    if (!QDir().mkpath(targetDir)) {
        addPackageError(diagnostics, QStringLiteral("cannot create package directory '%1'").arg(targetDir), span);
        return false;
    }

    QDirIterator it(sourceDir,
                    QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        const QString relative = QDir(sourceDir).relativeFilePath(info.absoluteFilePath());
        if (relative == QStringLiteral(".abel") || relative.startsWith(QStringLiteral(".abel/")))
            continue;
        const QString target = QDir(targetDir).absoluteFilePath(relative);
        if (info.isDir()) {
            if (!QDir().mkpath(target)) {
                addPackageError(diagnostics, QStringLiteral("cannot create package subdirectory '%1'").arg(target), span);
                return false;
            }
            continue;
        }
        if (info.isFile()) {
            if (!QDir().mkpath(QFileInfo(target).dir().absolutePath())) {
                addPackageError(diagnostics, QStringLiteral("cannot create package file directory '%1'").arg(QFileInfo(target).dir().absolutePath()), span);
                return false;
            }
            if (QFileInfo::exists(target) && !QFile::remove(target)) {
                addPackageError(diagnostics, QStringLiteral("cannot replace package file '%1'").arg(target), span);
                return false;
            }
            if (!QFile::copy(info.absoluteFilePath(), target)) {
                addPackageError(diagnostics,
                                QStringLiteral("cannot copy package file '%1' to '%2'")
                                    .arg(info.absoluteFilePath(), target),
                                span);
                return false;
            }
        }
    }
    return true;
}

QJsonObject registryEntryToJson(const PackageRegistryEntry& entry)
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), entry.name);
    object.insert(QStringLiteral("version"), entry.version);
    object.insert(QStringLiteral("path"), entry.path);
    object.insert(QStringLiteral("manifest"), entry.manifestFile);
    object.insert(QStringLiteral("entry"), entry.entry);
    object.insert(QStringLiteral("dependencies"), entry.dependencyCount);
    object.insert(QStringLiteral("backendArtifacts"), entry.backendArtifactCount);
    return object;
}

QJsonDocument registryIndexToJson(const PackageRegistryIndexResult& index)
{
    QJsonObject root;
    root.insert(QStringLiteral("formatVersion"), 1);
    root.insert(QStringLiteral("kind"), QStringLiteral("abel.localRegistry"));
    QJsonArray packages;
    for (const PackageRegistryEntry& entry : index.entries)
        packages.push_back(registryEntryToJson(entry));
    root.insert(QStringLiteral("packages"), packages);
    return QJsonDocument(root);
}

PackageRegistryEntry registryEntryFromJson(const QJsonObject& object,
                                           QList<Diagnostic>& diagnostics,
                                           const SourceSpan& span)
{
    PackageRegistryEntry entry;
    entry.name = requiredString(object, QStringLiteral("name"), diagnostics, span);
    entry.version = requiredString(object, QStringLiteral("version"), diagnostics, span);
    entry.path = requiredString(object, QStringLiteral("path"), diagnostics, span);
    entry.manifestFile = requiredString(object, QStringLiteral("manifest"), diagnostics, span);
    entry.entry = requiredString(object, QStringLiteral("entry"), diagnostics, span);
    entry.dependencyCount = object.value(QStringLiteral("dependencies")).toInt(-1);
    entry.backendArtifactCount = object.value(QStringLiteral("backendArtifacts")).toInt(-1);
    if (entry.dependencyCount < 0)
        addPackageError(diagnostics, QStringLiteral("registry index package entry requires integer field 'dependencies'"), span);
    if (entry.backendArtifactCount < 0)
        addPackageError(diagnostics, QStringLiteral("registry index package entry requires integer field 'backendArtifacts'"), span);
    return entry;
}

bool sameRegistryEntry(const PackageRegistryEntry& a, const PackageRegistryEntry& b)
{
    return a.name == b.name
        && a.version == b.version
        && a.path == b.path
        && a.manifestFile == b.manifestFile
        && a.entry == b.entry
        && a.dependencyCount == b.dependencyCount
        && a.backendArtifactCount == b.backendArtifactCount;
}

bool sameRegistryEntries(const QList<PackageRegistryEntry>& a, const QList<PackageRegistryEntry>& b)
{
    if (a.size() != b.size())
        return false;
    for (qsizetype i = 0; i < a.size(); ++i) {
        if (!sameRegistryEntry(a.at(i), b.at(i)))
            return false;
    }
    return true;
}

bool registryEntryLess(const PackageRegistryEntry& a, const PackageRegistryEntry& b)
{
    if (a.name != b.name)
        return a.name < b.name;
    SemVer av;
    SemVer bv;
    if (parseSemVerCore(a.version, av) && parseSemVerCore(b.version, bv)) {
        const int cmp = compareSemVer(av, bv);
        if (cmp != 0)
            return cmp < 0;
    }
    return a.version < b.version;
}

struct RegistryCandidate {
    PackageManifest package;
    QString sourceRoot;
    SemVer version;
};

std::optional<RegistryCandidate> findRegistryCandidate(const PackageDependency& dependency,
                                                       const QString& packageRoot,
                                                       const QString& projectRoot,
                                                       QList<Diagnostic>& diagnostics,
                                                       const SourceSpan& span)
{
    const auto registryRootValue = registryResolutionRoot(dependency.registry,
                                                          packageRoot,
                                                          projectRoot,
                                                          diagnostics,
                                                          span);
    if (!registryRootValue.has_value())
        return std::nullopt;
    const QString registryRoot = *registryRootValue;
    const QString packageDir = QDir(registryRoot).absoluteFilePath(dependency.name);
    const QFileInfo packageInfo(packageDir);
    if (!packageInfo.isDir()) {
        addPackageError(diagnostics,
                        QStringLiteral("registry dependency '%1' was not found under registry '%2'")
                            .arg(dependency.name, dependency.registry),
                        span);
        return std::nullopt;
    }

    std::optional<RegistryCandidate> best;
    auto considerPackageRoot = [&](const QString& sourceRoot) {
        if (!isPackageDirectory(sourceRoot))
            return;
        auto parsed = packageManifestFromDirectory(sourceRoot);
        diagnostics.append(parsed.diagnostics);
        if (!parsed.ok())
            return;
        if (parsed.package.name != dependency.name)
            return;
        QString versionError;
        if (!versionSatisfiesRequirement(parsed.package.version, dependency.version, &versionError))
            return;
        SemVer semver;
        if (!parseSemVerCore(parsed.package.version, semver))
            return;
        if (!best.has_value() || compareSemVer(semver, best->version) > 0) {
            best = RegistryCandidate{parsed.package, sourceRoot, semver};
        }
    };

    const QString indexFile = QDir(registryRoot).absoluteFilePath(packageLocalRegistryIndexFileName());
    if (QFileInfo(indexFile).isFile()) {
        auto index = checkLocalPackageRegistryIndex(registryRoot);
        diagnostics.append(index.diagnostics);
        if (!index.ok())
            return std::nullopt;
        for (const PackageRegistryEntry& entry : index.entries) {
            if (entry.name != dependency.name)
                continue;
            considerPackageRoot(absolutePathFrom(registryRoot, entry.path));
        }
    } else {
        const auto versionDirs = QDir(packageDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo& versionDir : versionDirs) {
            considerPackageRoot(versionDir.absoluteFilePath());
        }
    }

    if (!best.has_value()) {
        addPackageError(diagnostics,
                        dependency.version.isEmpty()
                            ? QStringLiteral("registry dependency '%1' has no usable versions in '%2'")
                                  .arg(dependency.name, dependency.registry)
                            : QStringLiteral("registry dependency '%1' has no version satisfying '%2' in '%3'")
                                  .arg(dependency.name, dependency.version, dependency.registry),
                        span);
        return std::nullopt;
    }

    const QString cachedRoot = registryPackageCachePath(projectRoot, best->package.name, best->package.version);
    if (!copyDirectoryRecursively(best->sourceRoot, cachedRoot, diagnostics, span))
        return std::nullopt;

    auto cached = packageManifestFromDirectory(cachedRoot);
    diagnostics.append(cached.diagnostics);
    if (!cached.ok())
        return std::nullopt;

    best->package = cached.package;
    best->sourceRoot = cachedRoot;
    return best;
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
                                  PackageBackendBuildSpec* build,
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
    node.qtVersion = optionalString(object, QStringLiteral("qtVersion"), currentAbelQtVersion());
    node.kit = optionalString(object, QStringLiteral("kit"), currentAbelQtKit());
    node.platform = optionalString(object, QStringLiteral("platform"), currentAbelPlatform());
    node.compiler = optionalString(object, QStringLiteral("compiler"), currentAbelCompiler());
    node.compilerVersion = optionalString(object, QStringLiteral("compilerVersion"), currentAbelCompilerVersion());
    node.cxxStandard = optionalString(object, QStringLiteral("cxxStandard"), currentAbelCxxStandard());
    node.abelAbi = optionalString(object, QStringLiteral("abelAbi"), currentAbelAbi());
    node.symbols = parseSymbols(object, node.backendId, diagnostics, span);
    node.state = ResourceNodeState::Unloaded;
    if (build)
        *build = parseBackendBuildSpec(object.value(QStringLiteral("build")), diagnostics, span);

    if (node.kind != QStringLiteral("qt_plugin"))
        addPackageError(diagnostics, QStringLiteral("backend artifact kind must be 'qt_plugin'"), span);
    if (node.iid != QStringLiteral(IAbelBackend_iid))
        addPackageError(diagnostics,
                        QStringLiteral("backend artifact iid must be '%1'").arg(QStringLiteral(IAbelBackend_iid)),
                        span);
    return node;
}

bool sameResolvedPackage(const PackageLockEntry& a, const PackageLockEntry& b)
{
    return a.name == b.name
        && a.version == b.version
        && a.kind == b.kind
        && absolutePathFrom(QString(), a.resolvedPath) == absolutePathFrom(QString(), b.resolvedPath);
}

void resolvePackageDependencies(const PackageManifest& package,
                                PackageLockResult& result,
                                QHash<QString, PackageLockEntry>& resolvedByName,
                                QSet<QString>& resolving)
{
    for (const PackageDependency& dependency : package.dependencies) {
        SourceSpan span;
        span.file = package.filePath;
        if (dependency.kind != QStringLiteral("path") && dependency.kind != QStringLiteral("registry")) {
            addPackageError(result.diagnostics,
                            QStringLiteral("dependency '%1' kind '%2' is not supported yet")
                                .arg(dependency.name, dependency.kind),
                            span);
            continue;
        }

        QString resolvedPath;
        PackageManifest resolvedPackage;
        if (dependency.kind == QStringLiteral("path")) {
            resolvedPath = absolutePathFrom(package.rootDir, dependency.path);
            const QFileInfo depInfo(resolvedPath);
            if (!depInfo.isDir() || !isPackageDirectory(resolvedPath)) {
                addPackageError(result.diagnostics,
                                QStringLiteral("path dependency '%1' does not point to an Abel package: %2")
                                    .arg(dependency.name, dependency.path),
                                span);
                continue;
            }

            auto parsed = packageManifestFromDirectory(resolvedPath);
            result.diagnostics.append(parsed.diagnostics);
            if (!parsed.ok())
                continue;
            resolvedPackage = parsed.package;
        } else {
            auto candidate = findRegistryCandidate(dependency, package.rootDir, result.rootDir, result.diagnostics, span);
            if (!candidate.has_value())
                continue;
            resolvedPath = candidate->sourceRoot;
            resolvedPackage = candidate->package;
        }

        const QFileInfo depInfo(resolvedPath);

        const QString key = dependency.kind == QStringLiteral("registry")
            ? QStringLiteral("registry:%1@%2").arg(resolvedPackage.name, resolvedPackage.version)
            : absolutePathFrom(QString(), resolvedPath);
        if (resolving.contains(key)) {
            addPackageError(result.diagnostics,
                            QStringLiteral("circular %1 dependency involving '%2' at %3")
                                .arg(dependency.kind,
                                     dependency.name,
                                     resolvedPath),
                            span);
            continue;
        }
        if (!dependency.name.isEmpty() && resolvedPackage.name != dependency.name) {
            addPackageError(result.diagnostics,
                            dependency.kind == QStringLiteral("path")
                                ? QStringLiteral("path dependency name mismatch: manifest asks for '%1' but package is '%2'")
                                      .arg(dependency.name, resolvedPackage.name)
                                : QStringLiteral("registry dependency name mismatch: manifest asks for '%1' but package is '%2'")
                                      .arg(dependency.name, resolvedPackage.name),
                            span);
            continue;
        }
        QString versionError;
        if (!versionSatisfiesRequirement(resolvedPackage.version, dependency.version, &versionError)) {
            addPackageError(result.diagnostics,
                            versionError.isEmpty()
                                ? QStringLiteral("%1 dependency '%2' requires version '%3' but package is '%4'")
                                      .arg(dependency.kind, dependency.name, dependency.version, resolvedPackage.version)
                                : QStringLiteral("%1 dependency '%2' version check failed: %3")
                                      .arg(dependency.kind, dependency.name, versionError),
                            span);
            continue;
        }
        PackageLockEntry entry;
        entry.name = resolvedPackage.name;
        entry.version = resolvedPackage.version;
        entry.versionRequirement = dependency.version;
        entry.kind = dependency.kind;
        entry.source = dependency.kind == QStringLiteral("registry") ? dependency.registry : dependency.path;
        entry.resolvedPath = dependency.kind == QStringLiteral("registry") ? resolvedPath : absolutePathFrom(QString(), resolvedPath);

        if (resolvedByName.contains(entry.name)) {
            const PackageLockEntry existing = resolvedByName.value(entry.name);
            if (!sameResolvedPackage(existing, entry)) {
                addPackageError(result.diagnostics,
                                QStringLiteral("dependency conflict for package '%1': already resolved %2 %3 at %4, but %5 from %6 resolves %7 at %8")
                                    .arg(entry.name,
                                         existing.kind,
                                         existing.version,
                                         existing.resolvedPath,
                                         entry.kind,
                                         entry.source,
                                         entry.version,
                                         entry.resolvedPath),
                                span);
                continue;
            }
            continue;
        }

        resolvedByName.insert(entry.name, entry);
        result.entries.push_back(entry);

        resolving.insert(key);
        resolvePackageDependencies(resolvedPackage, result, resolvedByName, resolving);
        resolving.remove(key);
    }
}

QJsonObject lockEntryToJson(const PackageLockEntry& entry)
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), entry.name);
    object.insert(QStringLiteral("version"), entry.version);
    if (!entry.versionRequirement.isEmpty())
        object.insert(QStringLiteral("versionRequirement"), entry.versionRequirement);
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
    if (!dependency.registry.isEmpty())
        object.insert(QStringLiteral("registry"), dependency.registry);
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

PackageLockEntry lockEntryFromJson(const QJsonObject& object,
                                   QList<Diagnostic>& diagnostics,
                                   const SourceSpan& span)
{
    PackageLockEntry entry;
    entry.name = requiredString(object, QStringLiteral("name"), diagnostics, span);
    entry.version = requiredString(object, QStringLiteral("version"), diagnostics, span);
    entry.versionRequirement = optionalString(object, QStringLiteral("versionRequirement"), QString());
    QString versionRequirementError;
    if (!validateVersionRequirementSyntax(entry.versionRequirement, &versionRequirementError)) {
        addPackageError(diagnostics,
                        QStringLiteral("lockfile package '%1' has invalid version requirement '%2': %3")
                            .arg(entry.name, entry.versionRequirement, versionRequirementError),
                        span);
    }
    entry.kind = requiredString(object, QStringLiteral("kind"), diagnostics, span);
    entry.source = requiredString(object, QStringLiteral("source"), diagnostics, span);
    entry.resolvedPath = requiredString(object, QStringLiteral("resolvedPath"), diagnostics, span);
    return entry;
}

void appendPackageArtifacts(const PackageManifest& package, PackageGraphResult& graph)
{
    for (qsizetype i = 0; i < package.backendArtifacts.size(); ++i) {
        PackageResolvedResource resource;
        resource.packageName = package.name;
        resource.packageRoot = package.rootDir;
        resource.node = package.backendArtifacts[i];
        if (i < package.backendArtifactBuilds.size())
            resource.build = package.backendArtifactBuilds[i];
        graph.backendArtifacts.push_back(resource);
    }
}

bool sameLockEntry(const PackageLockEntry& a, const PackageLockEntry& b)
{
    return a.name == b.name
        && a.version == b.version
        && a.versionRequirement == b.versionRequirement
        && a.kind == b.kind
        && a.source == b.source
        && absolutePathFrom(QString(), a.resolvedPath) == absolutePathFrom(QString(), b.resolvedPath);
}

bool sameLockEntries(const QList<PackageLockEntry>& a, const QList<PackageLockEntry>& b)
{
    if (a.size() != b.size())
        return false;
    for (qsizetype i = 0; i < a.size(); ++i) {
        if (!sameLockEntry(a[i], b[i]))
            return false;
    }
    return true;
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

bool upsertPackageDependency(QJsonObject& manifestObject,
                             const QString& manifestFile,
                             const PackageDependency& dependency,
                             bool& changed,
                             QList<Diagnostic>& diagnostics)
{
    QJsonArray dependencies;
    const QJsonValue existingValue = manifestObject.value(QStringLiteral("dependencies"));
    if (!existingValue.isUndefined()) {
        if (!existingValue.isArray()) {
            SourceSpan span;
            span.file = manifestFile;
            addPackageError(diagnostics, QStringLiteral("package manifest field 'dependencies' must be an array"), span);
            return false;
        }
        dependencies = existingValue.toArray();
    }

    const QJsonObject dependencyObject = dependencyToJson(dependency);
    bool replaced = false;
    for (qsizetype i = 0; i < dependencies.size(); ++i) {
        const QJsonValue raw = dependencies.at(i);
        if (!raw.isObject())
            continue;
        const QJsonObject object = raw.toObject();
        if (object.value(QStringLiteral("name")).toString() != dependency.name)
            continue;
        if (object == dependencyObject) {
            replaced = true;
            break;
        }
        dependencies.replace(i, dependencyObject);
        changed = true;
        replaced = true;
        break;
    }
    if (!replaced) {
        dependencies.push_back(dependencyObject);
        changed = true;
    }

    if (changed)
        manifestObject.insert(QStringLiteral("dependencies"), dependencies);
    return true;
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

QString packageLocalRegistryIndexFileName()
{
    return QString::fromLatin1(kPackageLocalRegistryIndexFileName);
}

QString packageCacheRoot(const QString& rootDir)
{
    return QDir(QFileInfo(rootDir).absoluteFilePath()).absoluteFilePath(QString::fromLatin1(kPackageCacheDirName)
                                                                       + QStringLiteral("/cache"));
}

QString packageBackendCacheDir(const QString& rootDir)
{
    return QDir(QFileInfo(rootDir).absoluteFilePath()).absoluteFilePath(QString::fromLatin1(kPackageCacheDirName)
                                                                       + QStringLiteral("/")
                                                                       + QString::fromLatin1(kPackageCacheBackendDir));
}

QString packageRegistryCacheDir(const QString& rootDir)
{
    return QDir(QFileInfo(rootDir).absoluteFilePath()).absoluteFilePath(QString::fromLatin1(kPackageCacheDirName)
                                                                       + QStringLiteral("/")
                                                                       + QString::fromLatin1(kPackageCachePackagesDir));
}

QString packageRegistryMirrorCacheDir(const QString& rootDir)
{
    return QDir(QFileInfo(rootDir).absoluteFilePath()).absoluteFilePath(QString::fromLatin1(kPackageCacheDirName)
                                                                       + QStringLiteral("/")
                                                                       + QString::fromLatin1(kPackageCacheRegistriesDir));
}

bool isPackageDirectory(const QString& path)
{
    const QFileInfo info(path);
    if (!info.isDir())
        return false;
    return QFileInfo(QDir(info.absoluteFilePath()).absoluteFilePath(packageManifestFileName())).isFile();
}

PackageRegistryIndexResult scanLocalPackageRegistry(const QString& registryDir)
{
    PackageRegistryIndexResult result;
    result.registryRoot = localRegistryRootPath(registryDir);
    result.indexFile = QDir(result.registryRoot).absoluteFilePath(packageLocalRegistryIndexFileName());

    const QFileInfo rootInfo(result.registryRoot);
    SourceSpan span;
    span.file = result.registryRoot;
    if (!rootInfo.exists()) {
        addPackageError(result.diagnostics,
                        QStringLiteral("registry directory '%1' does not exist").arg(registryDir),
                        span);
        return result;
    }
    if (!rootInfo.isDir()) {
        addPackageError(result.diagnostics,
                        QStringLiteral("registry path '%1' is not a directory").arg(registryDir),
                        span);
        return result;
    }

    const auto packageDirs = QDir(result.registryRoot).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                                     QDir::Name);
    QSet<QString> seenIdentities;
    for (const QFileInfo& packageInfo : packageDirs) {
        const QString packageDirName = packageInfo.fileName();
        if (packageDirName.startsWith(QLatin1Char('.')))
            continue;

        const auto versionDirs = QDir(packageInfo.absoluteFilePath()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                                                    QDir::Name);
        for (const QFileInfo& versionInfo : versionDirs) {
            if (versionInfo.fileName().startsWith(QLatin1Char('.')))
                continue;

            SourceSpan versionSpan;
            versionSpan.file = versionInfo.absoluteFilePath();
            if (!isPackageDirectory(versionInfo.absoluteFilePath())) {
                addPackageError(result.diagnostics,
                                QStringLiteral("registry entry '%1/%2' is not an Abel package")
                                    .arg(packageDirName, versionInfo.fileName()),
                                versionSpan);
                continue;
            }

            auto parsed = packageManifestFromDirectory(versionInfo.absoluteFilePath());
            result.diagnostics.append(parsed.diagnostics);
            if (!parsed.ok())
                continue;

            if (parsed.package.name != packageDirName) {
                addPackageError(result.diagnostics,
                                QStringLiteral("registry package directory '%1' contains manifest package '%2'")
                                    .arg(packageDirName, parsed.package.name),
                                SourceSpan{parsed.package.filePath});
                continue;
            }
            if (parsed.package.version != versionInfo.fileName()) {
                addPackageError(result.diagnostics,
                                QStringLiteral("registry version directory '%1/%2' contains manifest version '%3'")
                                    .arg(packageDirName, versionInfo.fileName(), parsed.package.version),
                                SourceSpan{parsed.package.filePath});
                continue;
            }
            SemVer ignored;
            if (!parseSemVerCore(parsed.package.version, ignored)) {
                addPackageError(result.diagnostics,
                                QStringLiteral("registry package '%1' version '%2' is not SemVer major.minor.patch")
                                    .arg(parsed.package.name, parsed.package.version),
                                SourceSpan{parsed.package.filePath});
                continue;
            }

            const QString identity = parsed.package.name + QLatin1Char('@') + parsed.package.version;
            if (seenIdentities.contains(identity)) {
                addPackageError(result.diagnostics,
                                QStringLiteral("registry contains duplicate package identity '%1'")
                                    .arg(identity),
                                SourceSpan{parsed.package.filePath});
                continue;
            }
            seenIdentities.insert(identity);

            PackageRegistryEntry entry;
            entry.name = parsed.package.name;
            entry.version = parsed.package.version;
            entry.path = QDir(result.registryRoot).relativeFilePath(parsed.package.rootDir);
            entry.manifestFile = QDir(result.registryRoot).relativeFilePath(parsed.package.filePath);
            entry.entry = parsed.package.entry;
            entry.dependencyCount = parsed.package.dependencies.size();
            entry.backendArtifactCount = parsed.package.backendArtifacts.size();
            result.entries.push_back(entry);
        }
    }

    std::sort(result.entries.begin(), result.entries.end(), registryEntryLess);
    return result;
}

PackageRegistryIndexResult writeLocalPackageRegistryIndex(const QString& registryDir)
{
    const QString registryRoot = localRegistryRootPath(registryDir);
    PackageRegistryIndexResult result;
    result.registryRoot = registryRoot;
    result.indexFile = QDir(registryRoot).absoluteFilePath(packageLocalRegistryIndexFileName());

    QFileInfo rootInfo(registryRoot);
    if (rootInfo.exists() && !rootInfo.isDir()) {
        SourceSpan span;
        span.file = registryRoot;
        addPackageError(result.diagnostics,
                        QStringLiteral("registry path '%1' is not a directory").arg(registryDir),
                        span);
        return result;
    }
    if (!rootInfo.exists() && !QDir().mkpath(registryRoot)) {
        SourceSpan span;
        span.file = registryRoot;
        addPackageError(result.diagnostics,
                        QStringLiteral("cannot create registry directory '%1'").arg(registryRoot),
                        span);
        return result;
    }

    result = scanLocalPackageRegistry(registryRoot);
    if (!result.ok())
        return result;

    QFile file(result.indexFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        SourceSpan span;
        span.file = result.indexFile;
        addPackageError(result.diagnostics,
                        QStringLiteral("cannot write registry index '%1'").arg(result.indexFile),
                        span);
        return result;
    }
    const QByteArray bytes = registryIndexToJson(result).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size()) {
        SourceSpan span;
        span.file = result.indexFile;
        addPackageError(result.diagnostics,
                        QStringLiteral("failed to write registry index '%1'").arg(result.indexFile),
                        span);
        return result;
    }
    result.written = true;
    return result;
}

PackageRegistryIndexResult checkLocalPackageRegistryIndex(const QString& registryDir)
{
    PackageRegistryIndexResult scanned = scanLocalPackageRegistry(registryDir);
    if (!scanned.ok())
        return scanned;

    PackageRegistryIndexResult result = scanned;
    QFile file(result.indexFile);
    SourceSpan span;
    span.file = result.indexFile;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.stale = true;
        addPackageError(result.diagnostics,
                        QStringLiteral("registry index '%1' is missing; run 'abel package registry index <registry-dir>'")
                            .arg(result.indexFile),
                        span);
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        result.stale = true;
        addPackageError(result.diagnostics,
                        parseError.error == QJsonParseError::NoError
                            ? QStringLiteral("registry index JSON must be an object")
                            : QStringLiteral("invalid registry index JSON: %1").arg(parseError.errorString()),
                        span);
        return result;
    }

    const QJsonObject object = doc.object();
    if (object.value(QStringLiteral("formatVersion")).toInt() != 1) {
        result.stale = true;
        addPackageError(result.diagnostics, QStringLiteral("registry index formatVersion must be 1"), span);
    }
    if (object.value(QStringLiteral("kind")).toString() != QStringLiteral("abel.localRegistry")) {
        result.stale = true;
        addPackageError(result.diagnostics, QStringLiteral("registry index kind must be 'abel.localRegistry'"), span);
    }

    QList<PackageRegistryEntry> indexedEntries;
    const QJsonValue packagesValue = object.value(QStringLiteral("packages"));
    if (!packagesValue.isArray()) {
        result.stale = true;
        addPackageError(result.diagnostics, QStringLiteral("registry index requires array field 'packages'"), span);
    } else {
        for (const QJsonValue& raw : packagesValue.toArray()) {
            if (!raw.isObject()) {
                result.stale = true;
                addPackageError(result.diagnostics, QStringLiteral("registry index package entry must be an object"), span);
                continue;
            }
            indexedEntries.push_back(registryEntryFromJson(raw.toObject(), result.diagnostics, span));
        }
    }
    std::sort(indexedEntries.begin(), indexedEntries.end(), registryEntryLess);

    if (result.diagnostics.isEmpty() && !sameRegistryEntries(indexedEntries, scanned.entries)) {
        result.stale = true;
        addPackageError(result.diagnostics,
                        QStringLiteral("registry index '%1' is stale; run 'abel package registry index <registry-dir>'")
                            .arg(result.indexFile),
                        span);
    }
    result.entries = scanned.entries;
    return result;
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
        "abel update .\n"
        "abel build .\n"
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

    QHash<QString, PackageLockEntry> resolvedByName;
    QSet<QString> resolving;
    resolving.insert(absolutePathFrom(QString(), result.rootDir));
    resolvePackageDependencies(parsed.package, result, resolvedByName, resolving);
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

PackageLockResult packageLockFromFile(const QString& lockFile)
{
    PackageLockResult result;
    result.lockFile = QFileInfo(lockFile).absoluteFilePath();
    QFile file(lockFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        SourceSpan span;
        span.file = result.lockFile;
        addPackageError(result.diagnostics, QStringLiteral("cannot open package lockfile '%1'").arg(result.lockFile), span);
        return result;
    }

    SourceSpan span;
    span.file = result.lockFile;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        addPackageError(result.diagnostics, QStringLiteral("invalid package lockfile JSON: %1").arg(parseError.errorString()), span);
        return result;
    }
    if (!doc.isObject()) {
        addPackageError(result.diagnostics, QStringLiteral("package lockfile JSON must be an object"), span);
        return result;
    }

    const QJsonObject object = doc.object();
    const QJsonValue rootValue = object.value(QStringLiteral("root"));
    if (!rootValue.isObject()) {
        addPackageError(result.diagnostics, QStringLiteral("package lockfile requires object field 'root'"), span);
    } else {
        const QJsonObject root = rootValue.toObject();
        result.rootName = requiredString(root, QStringLiteral("name"), result.diagnostics, span);
        result.rootVersion = requiredString(root, QStringLiteral("version"), result.diagnostics, span);
        result.rootDir = requiredString(root, QStringLiteral("path"), result.diagnostics, span);
    }

    const QJsonValue packagesValue = object.value(QStringLiteral("packages"));
    if (!packagesValue.isArray()) {
        addPackageError(result.diagnostics, QStringLiteral("package lockfile requires array field 'packages'"), span);
    } else {
        for (const QJsonValue& raw : packagesValue.toArray()) {
            if (!raw.isObject()) {
                addPackageError(result.diagnostics, QStringLiteral("package lockfile package entry must be an object"), span);
                continue;
            }
            result.entries.push_back(lockEntryFromJson(raw.toObject(), result.diagnostics, span));
        }
    }

    return result;
}

PackageGraphResult packageGraphFromDirectory(const QString& dir)
{
    PackageGraphResult graph;
    auto parsed = packageManifestFromDirectory(dir);
    graph.diagnostics.append(parsed.diagnostics);
    if (!parsed.ok())
        return graph;
    graph.root = parsed.package;
    graph.lockFile = QDir(graph.root.rootDir).absoluteFilePath(packageLockFileName());

    PackageLockResult lock;
    const bool usedLockFile = QFileInfo(graph.lockFile).isFile();
    if (usedLockFile) {
        lock = packageLockFromFile(graph.lockFile);
    } else {
        lock = resolvePackageLock(graph.root.rootDir);
        lock.lockFile = graph.lockFile;
    }
    graph.diagnostics.append(lock.diagnostics);
    if (!lock.ok())
        return graph;
    graph.entries = lock.entries;

    if (usedLockFile) {
        const PackageLockResult current = resolvePackageLock(graph.root.rootDir);
        graph.diagnostics.append(current.diagnostics);
        if (!current.ok())
            return graph;
        if (!sameLockEntries(current.entries, lock.entries)) {
            SourceSpan span;
            span.file = graph.lockFile;
            addPackageError(graph.diagnostics,
                            QStringLiteral("package lockfile is stale; run 'abel update' or 'abel build'"),
                            span);
            return graph;
        }
    }

    appendPackageArtifacts(graph.root, graph);

    const QString rootKey = absolutePathFrom(QString(), graph.root.rootDir);
    if (!lock.rootDir.isEmpty() && absolutePathFrom(QString(), lock.rootDir) != rootKey) {
        SourceSpan span;
        span.file = graph.lockFile;
        addPackageError(graph.diagnostics,
                        QStringLiteral("lockfile root path '%1' does not match package root '%2'")
                            .arg(lock.rootDir, graph.root.rootDir),
                        span);
        return graph;
    }
    if (!lock.rootName.isEmpty() && lock.rootName != graph.root.name) {
        SourceSpan span;
        span.file = graph.lockFile;
        addPackageError(graph.diagnostics,
                        QStringLiteral("lockfile root package '%1' does not match manifest package '%2'")
                            .arg(lock.rootName, graph.root.name),
                        span);
        return graph;
    }

    for (const PackageLockEntry& entry : lock.entries) {
        SourceSpan span;
        span.file = graph.lockFile;
        if (entry.kind != QStringLiteral("path") && entry.kind != QStringLiteral("registry")) {
            addPackageError(graph.diagnostics,
                            QStringLiteral("lockfile package '%1' kind '%2' is not supported yet")
                                .arg(entry.name, entry.kind),
                            span);
            continue;
        }

        auto dependency = packageManifestFromDirectory(entry.resolvedPath);
        graph.diagnostics.append(dependency.diagnostics);
        if (!dependency.ok())
            continue;
        if (dependency.package.name != entry.name) {
            addPackageError(graph.diagnostics,
                            QStringLiteral("lockfile package name mismatch: lock has '%1' but package is '%2'")
                                .arg(entry.name, dependency.package.name),
                            span);
            continue;
        }
        if (!entry.version.isEmpty() && dependency.package.version != entry.version) {
            addPackageError(graph.diagnostics,
                            QStringLiteral("lockfile package version mismatch for '%1': lock has '%2' but package is '%3'")
                                .arg(entry.name, entry.version, dependency.package.version),
                            span);
            continue;
        }
        QString versionError;
        if (!versionSatisfiesRequirement(dependency.package.version, entry.versionRequirement, &versionError)) {
            addPackageError(graph.diagnostics,
                            versionError.isEmpty()
                                ? QStringLiteral("lockfile package '%1' requires version '%2' but package is '%3'")
                                      .arg(entry.name, entry.versionRequirement, dependency.package.version)
                                : QStringLiteral("lockfile package '%1' version check failed: %2")
                                      .arg(entry.name, versionError),
                            span);
            continue;
        }
        graph.dependencies.push_back(dependency.package);
        appendPackageArtifacts(dependency.package, graph);
    }

    return graph;
}

PackageGraphResult updatePackageGraph(const QString& dir)
{
    PackageGraphResult graph;
    const PackageLockResult lock = updatePackageLock(dir);
    graph.diagnostics.append(lock.diagnostics);
    if (!lock.ok())
        return graph;
    graph = packageGraphFromDirectory(lock.rootDir);
    return graph;
}

QStringList packageSourceFiles(const PackageManifest& package, bool includeEntry)
{
    QStringList files;
    const QString entry = package.entryFilePath();
    const QDir srcDir(QDir(package.rootDir).absoluteFilePath(QStringLiteral("src")));
    if (srcDir.exists()) {
        QDirIterator it(srcDir.absolutePath(),
                        QStringList{QStringLiteral("*.abel")},
                        QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = QFileInfo(it.next()).absoluteFilePath();
            if (path != entry)
                files.push_back(path);
        }
    }
    files.sort();
    if (includeEntry && !entry.isEmpty())
        files.push_back(entry);
    return files;
}

QStringList packageGraphSourceFiles(const PackageGraphResult& graph)
{
    QStringList files;
    for (const PackageManifest& dependency : graph.dependencies)
        files.append(packageSourceFiles(dependency, false));
    files.append(packageSourceFiles(graph.root, true));
    return files;
}

QList<PackageSourceFile> packageGraphSourceFileEntries(const PackageGraphResult& graph)
{
    QList<PackageSourceFile> entries;
    for (const PackageManifest& dependency : graph.dependencies) {
        for (const QString& path : packageSourceFiles(dependency, false)) {
            PackageSourceFile entry;
            entry.packageName = dependency.name;
            entry.path = path;
            entry.fromDependency = true;
            entry.entry = false;
            entries.push_back(entry);
        }
    }
    for (const QString& path : packageSourceFiles(graph.root, true)) {
        PackageSourceFile entry;
        entry.packageName = graph.root.name;
        entry.path = path;
        entry.fromDependency = false;
        entry.entry = path == graph.root.entryFilePath();
        entries.push_back(entry);
    }
    return entries;
}

PackageBackendCacheResult updatePackageBackendCache(const PackageGraphResult& graph)
{
    PackageBackendCacheResult result;
    result.rootDir = graph.root.rootDir;
    result.cacheDir = packageBackendCacheDir(result.rootDir);
    result.diagnostics.append(graph.diagnostics);
    if (!graph.ok())
        return result;

    if (graph.backendArtifacts.isEmpty())
        return result;

    if (!QDir().mkpath(result.cacheDir)) {
        SourceSpan span;
        span.file = result.cacheDir;
        addPackageError(result.diagnostics,
                        QStringLiteral("cannot create backend artifact cache directory '%1'").arg(result.cacheDir),
                        span);
        return result;
    }

    for (const PackageResolvedResource& resource : graph.backendArtifacts) {
        if (!buildBackendArtifact(resource, result.diagnostics))
            continue;

        const QString sourcePath = backendArtifactSourcePath(resource);
        const QFileInfo sourceInfo(sourcePath);
        SourceSpan span;
        span.file = QDir(resource.packageRoot).absoluteFilePath(packageManifestFileName());
        if (!sourceInfo.isFile()) {
            addPackageError(result.diagnostics,
                            QStringLiteral("backend artifact '%1' from package '%2' does not exist")
                                .arg(resource.node.path, resource.packageName),
                            span);
            continue;
        }

        const QString cachedPath = backendArtifactCachedPath(result.rootDir, resource);
        const QFileInfo cachedInfo(cachedPath);
        if (!QDir().mkpath(cachedInfo.dir().absolutePath())) {
            SourceSpan cacheSpan;
            cacheSpan.file = cachedInfo.dir().absolutePath();
            addPackageError(result.diagnostics,
                            QStringLiteral("cannot create backend artifact cache directory '%1'")
                                .arg(cachedInfo.dir().absolutePath()),
                            cacheSpan);
            continue;
        }

        const QString canonicalSource = canonicalOrAbsoluteFilePath(sourceInfo);
        const QString canonicalCache = canonicalOrAbsoluteFilePath(cachedInfo);

        if (canonicalSource != canonicalCache) {
            if (QFileInfo::exists(cachedPath) && !QFile::remove(cachedPath)) {
                SourceSpan cacheSpan;
                cacheSpan.file = cachedPath;
                addPackageError(result.diagnostics,
                                QStringLiteral("cannot replace cached backend artifact '%1'").arg(cachedPath),
                                cacheSpan);
                continue;
            }
            if (!QFile::copy(sourcePath, cachedPath)) {
                SourceSpan cacheSpan;
                cacheSpan.file = cachedPath;
                addPackageError(result.diagnostics,
                                QStringLiteral("cannot copy backend artifact '%1' to cache '%2'")
                                    .arg(sourcePath, cachedPath),
                                cacheSpan);
                continue;
            }
        }
        const QString metadataPath = backendArtifactCacheMetadataPath(cachedPath);
        if (!writeBackendArtifactCacheMetadata(metadataPath, resource, sourcePath, sourceInfo, result.diagnostics))
            continue;

        PackageCachedResource cached;
        cached.packageName = resource.packageName;
        cached.packageRoot = resource.packageRoot;
        cached.sourcePath = sourcePath;
        cached.cachedPath = cachedPath;
        cached.metadataPath = metadataPath;
        cached.node = resource.node;
        cached.node.path = cachedPath;
        cached.node.state = ResourceNodeState::Unloaded;
        cached.node.lastError.clear();
        result.resources.push_back(cached);
    }

    return result;
}

QList<PackageResolvedResource> cachedPackageBackendArtifacts(const PackageGraphResult& graph)
{
    QList<PackageResolvedResource> resources;
    resources.reserve(graph.backendArtifacts.size());
    if (graph.root.rootDir.isEmpty()) {
        resources = graph.backendArtifacts;
        return resources;
    }

    for (const PackageResolvedResource& resource : graph.backendArtifacts) {
        PackageResolvedResource resolved = resource;
        const QString cachedPath = backendArtifactCachedPath(graph.root.rootDir, resource);
        const QString metadataPath = backendArtifactCacheMetadataPath(cachedPath);
        const QString sourcePath = backendArtifactSourcePath(resource);
        if (QFileInfo(cachedPath).isFile()
            && backendArtifactCacheMetadataMatches(metadataPath, resource, sourcePath)) {
            resolved.packageRoot = graph.root.rootDir;
            resolved.node.path = cachedPath;
            resolved.node.state = ResourceNodeState::Unloaded;
            resolved.node.lastError.clear();
        }
        resources.push_back(resolved);
    }
    return resources;
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

    if (!upsertPackageDependency(manifestObject,
                                 result.manifestFile,
                                 dependency,
                                 result.changed,
                                 result.diagnostics)) {
        return result;
    }

    if (result.changed) {
        if (!writeManifestObject(result.manifestFile, manifestObject, result.diagnostics))
            return result;
    }

    const auto lock = updatePackageLock(result.rootDir);
    copyLockResultIntoChange(result, lock);
    return result;
}

PackageDependencyChangeResult addRegistryPackageDependency(const QString& dir,
                                                          const QString& packageName,
                                                          const QString& versionRequirement,
                                                          const QString& registryDir)
{
    PackageDependencyChangeResult result;
    QJsonObject manifestObject;
    if (!readManifestObjectForUpdate(dir, manifestObject, result.manifestFile, result.rootDir, result.diagnostics))
        return result;

    auto rootPackage = packageManifestFromJson(manifestObject, result.rootDir, SourceSpan{result.manifestFile});
    result.diagnostics.append(rootPackage.diagnostics);
    if (!rootPackage.ok())
        return result;

    const QString name = packageName.trimmed();
    if (name.isEmpty()) {
        SourceSpan span;
        span.file = result.manifestFile;
        addPackageError(result.diagnostics, QStringLiteral("registry dependency name must not be empty"), span);
        return result;
    }

    const QString requirement = versionRequirement.trimmed();
    QString requirementError;
    if (!validateVersionRequirementSyntax(requirement, &requirementError)) {
        SourceSpan span;
        span.file = result.manifestFile;
        addPackageError(result.diagnostics,
                        QStringLiteral("registry dependency '%1' has invalid version requirement '%2': %3")
                            .arg(name, requirement, requirementError),
                        span);
        return result;
    }

    PackageDependency dependency;
    dependency.name = name;
    dependency.kind = QStringLiteral("registry");
    if (registryDir.contains(QStringLiteral("://"))) {
        const auto registryRoot = registryResolutionRoot(registryDir,
                                                         result.rootDir,
                                                         result.rootDir,
                                                         result.diagnostics,
                                                         SourceSpan{result.manifestFile});
        if (!registryRoot.has_value())
            return result;
        dependency.registry = registryDir;
    } else {
        QFileInfo registryInfo(registryDir);
        if (!registryInfo.isAbsolute())
            registryInfo = QFileInfo(QDir::current().absoluteFilePath(registryDir));
        const QString registryRoot = canonicalOrAbsoluteFilePath(registryInfo);
        if (!QFileInfo(registryRoot).isDir()) {
            SourceSpan span;
            span.file = registryRoot;
            addPackageError(result.diagnostics,
                            QStringLiteral("registry directory '%1' does not exist").arg(registryDir),
                            span);
            return result;
        }
        dependency.registry = QDir(result.rootDir).relativeFilePath(registryRoot);
    }
    dependency.version = requirement;
    result.dependency = dependency;

    if (!upsertPackageDependency(manifestObject,
                                 result.manifestFile,
                                 dependency,
                                 result.changed,
                                 result.diagnostics)) {
        return result;
    }

    if (result.changed) {
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
            result.dependency.registry = object.value(QStringLiteral("registry")).toString();
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

PackagePublishResult publishPackageToLocalRegistry(const QString& dir,
                                                   const QString& registryDir,
                                                   bool overwrite)
{
    PackagePublishResult result;
    auto parsed = packageManifestFromDirectory(dir);
    result.diagnostics.append(parsed.diagnostics);
    if (!parsed.ok())
        return result;
    result.package = parsed.package;

    SourceSpan span;
    span.file = parsed.package.filePath;
    if (parsed.package.name.contains(QLatin1Char('/'))
        || parsed.package.name.contains(QLatin1Char('\\'))
        || parsed.package.name == QStringLiteral(".")
        || parsed.package.name == QStringLiteral("..")) {
        addPackageError(result.diagnostics,
                        QStringLiteral("package name '%1' cannot be used as a local registry path segment")
                            .arg(parsed.package.name),
                        span);
        return result;
    }

    const QString registryInput = registryDir.trimmed().isEmpty() ? QStringLiteral(".") : registryDir;
    QFileInfo registryInfo(registryInput);
    if (!registryInfo.isAbsolute())
        registryInfo = QFileInfo(QDir::current().absoluteFilePath(registryInput));
    result.registryRoot = localRegistryRootPath(registryInput);
    if (registryInfo.exists() && !registryInfo.isDir()) {
        SourceSpan registrySpan;
        registrySpan.file = result.registryRoot;
        addPackageError(result.diagnostics,
                        QStringLiteral("registry path '%1' is not a directory").arg(registryDir),
                        registrySpan);
        return result;
    }
    if (!QDir().mkpath(result.registryRoot)) {
        SourceSpan registrySpan;
        registrySpan.file = result.registryRoot;
        addPackageError(result.diagnostics,
                        QStringLiteral("cannot create registry directory '%1'").arg(result.registryRoot),
                        registrySpan);
        return result;
    }

    const QString packageDir = QDir(result.registryRoot).absoluteFilePath(parsed.package.name);
    result.targetDir = QDir(packageDir).absoluteFilePath(parsed.package.version);
    const QFileInfo targetInfo(result.targetDir);
    const QString sourceRoot = canonicalOrAbsoluteFilePath(QFileInfo(parsed.package.rootDir));
    const QString targetRoot = targetInfo.exists() ? canonicalOrAbsoluteFilePath(targetInfo) : targetInfo.absoluteFilePath();
    if (targetRoot == sourceRoot || targetRoot.startsWith(sourceRoot + QLatin1Char('/'))) {
        addPackageError(result.diagnostics,
                        QStringLiteral("cannot publish package into itself: target '%1' is inside source '%2'")
                            .arg(targetRoot, sourceRoot),
                        span);
        return result;
    }

    result.overwritten = QFileInfo::exists(result.targetDir);
    if (result.overwritten && !overwrite) {
        SourceSpan targetSpan;
        targetSpan.file = result.targetDir;
        addPackageError(result.diagnostics,
                        QStringLiteral("package '%1' version '%2' already exists in registry; use --overwrite to replace it")
                            .arg(parsed.package.name, parsed.package.version),
                        targetSpan);
        return result;
    }

    if (!copyDirectoryRecursively(parsed.package.rootDir, result.targetDir, result.diagnostics, span))
        return result;

    auto published = packageManifestFromDirectory(result.targetDir);
    result.diagnostics.append(published.diagnostics);
    if (!published.ok())
        return result;
    if (published.package.name != parsed.package.name || published.package.version != parsed.package.version) {
        SourceSpan targetSpan;
        targetSpan.file = QDir(result.targetDir).absoluteFilePath(packageManifestFileName());
        addPackageError(result.diagnostics,
                        QStringLiteral("published package identity mismatch: expected %1 %2, got %3 %4")
                            .arg(parsed.package.name,
                                 parsed.package.version,
                                 published.package.name,
                                 published.package.version),
                        targetSpan);
        return result;
    }
    result.package = published.package;

    const auto indexed = writeLocalPackageRegistryIndex(result.registryRoot);
    result.indexFile = indexed.indexFile;
    result.diagnostics.append(indexed.diagnostics);
    if (!indexed.ok())
        return result;
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
    if (!result.package.version.isEmpty()) {
        SemVer ignored;
        if (!parseSemVerCore(result.package.version, ignored)) {
            addPackageError(result.diagnostics,
                            QStringLiteral("package version '%1' is not supported; expected SemVer major.minor.patch")
                                .arg(result.package.version),
                            span);
        }
    }

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
                PackageBackendBuildSpec build;
                result.package.backendArtifacts.push_back(parseBackendArtifact(artifact.toObject(), &build, result.diagnostics, span));
                result.package.backendArtifactBuilds.push_back(build);
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
