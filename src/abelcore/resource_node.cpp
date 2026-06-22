#include "abelcore/resource_node.h"

#include "abelcore/backend_interface.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QPluginLoader>

namespace abel {

namespace {

constexpr auto kBackendIid = "org.abel.IAbelBackend/1.0";

void addError(QList<Diagnostic>& diagnostics, const QString& message, const SourceSpan& span)
{
    Diagnostic d;
    d.severity = Severity::Error;
    d.code = QStringLiteral("E0612");
    d.message = message;
    d.primary = span;
    diagnostics.push_back(d);
}

void addLoadError(QList<Diagnostic>& diagnostics, const QString& message)
{
    Diagnostic d;
    d.severity = Severity::Error;
    d.code = QStringLiteral("E0613");
    d.message = message;
    diagnostics.push_back(d);
}

QString requiredString(const QJsonObject& object, const QString& key, QList<Diagnostic>& diagnostics, const SourceSpan& span)
{
    const QJsonValue value = object.value(key);
    if (!value.isString() || value.toString().isEmpty()) {
        addError(diagnostics, QStringLiteral("resource node requires non-empty string field '%1'").arg(key), span);
        return {};
    }
    return value.toString();
}

QString resolvedPluginPath(const ResourceNode& node, const QString& baseDir)
{
    QFileInfo info(node.path);
    if (info.isRelative() && !baseDir.isEmpty())
        info = QFileInfo(QDir(baseDir).absoluteFilePath(node.path));
    return info.absoluteFilePath();
}

bool sameSignature(const BackendFunctionDesc& expected, const AbelBackendFunction& actual)
{
    return expected.returnType == actual.returnType
        && expected.params == actual.params
        && expected.variadic == actual.variadic;
}

} // namespace

QString resourceNodeStateName(ResourceNodeState state)
{
    switch (state) {
    case ResourceNodeState::Unloaded: return QStringLiteral("unloaded");
    case ResourceNodeState::Loaded: return QStringLiteral("loaded");
    case ResourceNodeState::Failed: return QStringLiteral("failed");
    }
    return QStringLiteral("failed");
}

ResourceNodeState resourceNodeStateFromName(const QString& name, bool* ok)
{
    if (ok)
        *ok = true;
    if (name == QStringLiteral("unloaded"))
        return ResourceNodeState::Unloaded;
    if (name == QStringLiteral("loaded"))
        return ResourceNodeState::Loaded;
    if (name == QStringLiteral("failed"))
        return ResourceNodeState::Failed;
    if (ok)
        *ok = false;
    return ResourceNodeState::Failed;
}

ResourceNodeParseResult resourceNodeFromJson(const QJsonObject& object, const SourceSpan& span)
{
    ResourceNodeParseResult result;
    result.node.id = requiredString(object, QStringLiteral("id"), result.diagnostics, span);
    result.node.kind = requiredString(object, QStringLiteral("kind"), result.diagnostics, span);
    result.node.path = requiredString(object, QStringLiteral("path"), result.diagnostics, span);
    result.node.iid = requiredString(object, QStringLiteral("iid"), result.diagnostics, span);
    result.node.backendId = requiredString(object, QStringLiteral("backendId"), result.diagnostics, span);
    result.node.qtVersion = requiredString(object, QStringLiteral("qtVersion"), result.diagnostics, span);
    result.node.kit = requiredString(object, QStringLiteral("kit"), result.diagnostics, span);

    if (result.node.kind != QStringLiteral("qt_plugin"))
        addError(result.diagnostics, QStringLiteral("resource node kind must be 'qt_plugin'"), span);
    if (result.node.iid != QString::fromLatin1(kBackendIid))
        addError(result.diagnostics, QStringLiteral("resource node iid must be '%1'").arg(QString::fromLatin1(kBackendIid)), span);

    const QJsonValue symbols = object.value(QStringLiteral("symbols"));
    if (!symbols.isArray() || symbols.toArray().isEmpty()) {
        addError(result.diagnostics, QStringLiteral("resource node requires non-empty array field 'symbols'"), span);
    } else {
        for (const QJsonValue& value : symbols.toArray()) {
            if (!value.isString() || value.toString().isEmpty()) {
                addError(result.diagnostics, QStringLiteral("resource node symbols must be non-empty strings"), span);
                continue;
            }
            const QString symbol = value.toString();
            if (!result.node.backendId.isEmpty() && !symbol.startsWith(result.node.backendId + QStringLiteral(".")))
                addError(result.diagnostics, QStringLiteral("resource node symbol '%1' does not belong to backend '%2'").arg(symbol, result.node.backendId), span);
            result.node.symbols.push_back(symbol);
        }
    }

    const QJsonValue state = object.value(QStringLiteral("state"));
    if (state.isUndefined()) {
        result.node.state = ResourceNodeState::Unloaded;
    } else if (!state.isString()) {
        addError(result.diagnostics, QStringLiteral("resource node field 'state' must be a string"), span);
    } else {
        bool ok = false;
        result.node.state = resourceNodeStateFromName(state.toString(), &ok);
        if (!ok)
            addError(result.diagnostics, QStringLiteral("resource node state must be unloaded, loaded, or failed"), span);
    }

    const QJsonValue lastError = object.value(QStringLiteral("lastError"));
    if (!lastError.isUndefined()) {
        if (!lastError.isString())
            addError(result.diagnostics, QStringLiteral("resource node field 'lastError' must be a string"), span);
        else
            result.node.lastError = lastError.toString();
    }

    return result;
}

ResourceNodeParseResult resourceNodeFromJsonText(const QString& text, const QString& file)
{
    SourceSpan span;
    span.file = file;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        ResourceNodeParseResult result;
        addError(result.diagnostics, QStringLiteral("invalid resource node JSON: %1").arg(parseError.errorString()), span);
        return result;
    }
    if (!doc.isObject()) {
        ResourceNodeParseResult result;
        addError(result.diagnostics, QStringLiteral("resource node JSON must be an object"), span);
        return result;
    }
    return resourceNodeFromJson(doc.object(), span);
}

ResourceNodeLoadResult::ResourceNodeLoadResult() = default;
ResourceNodeLoadResult::~ResourceNodeLoadResult() = default;
ResourceNodeLoadResult::ResourceNodeLoadResult(ResourceNodeLoadResult&&) noexcept = default;
ResourceNodeLoadResult& ResourceNodeLoadResult::operator=(ResourceNodeLoadResult&&) noexcept = default;

ResourceNodeLoadResult loadBackendResourceNode(const ResourceNode& node,
                                               BackendRegistry& registry,
                                               const QString& baseDir)
{
    ResourceNodeLoadResult result;
    result.node = node;
    result.node.state = ResourceNodeState::Failed;

    if (node.kind != QStringLiteral("qt_plugin")) {
        addLoadError(result.diagnostics, QStringLiteral("resource node kind must be 'qt_plugin'"));
        result.node.lastError = result.diagnostics.back().message;
        return result;
    }
    if (node.iid != QString::fromLatin1(kBackendIid)) {
        addLoadError(result.diagnostics,
                     QStringLiteral("resource node iid must be '%1'").arg(QString::fromLatin1(kBackendIid)));
        result.node.lastError = result.diagnostics.back().message;
        return result;
    }

    const QString pluginPath = resolvedPluginPath(node, baseDir);
    auto loader = std::make_shared<QPluginLoader>(pluginPath);
    const QString metadataIid = loader->metaData().value(QStringLiteral("IID")).toString();
    if (!metadataIid.isEmpty() && metadataIid != node.iid) {
        addLoadError(result.diagnostics,
                     QStringLiteral("plugin IID '%1' does not match resource IID '%2'").arg(metadataIid, node.iid));
        result.node.lastError = result.diagnostics.back().message;
        return result;
    }

    QObject* object = loader->instance();
    if (!object) {
        addLoadError(result.diagnostics,
                     QStringLiteral("failed to load plugin '%1': %2").arg(pluginPath, loader->errorString()));
        result.node.lastError = result.diagnostics.back().message;
        return result;
    }

    auto* backend = qobject_cast<IAbelBackend*>(object);
    if (!backend) {
        addLoadError(result.diagnostics,
                     QStringLiteral("plugin '%1' does not implement IAbelBackend").arg(pluginPath));
        result.node.lastError = result.diagnostics.back().message;
        return result;
    }
    if (backend->backendId() != node.backendId) {
        addLoadError(result.diagnostics,
                     QStringLiteral("plugin backendId '%1' does not match resource backendId '%2'")
                         .arg(backend->backendId(), node.backendId));
        result.node.lastError = result.diagnostics.back().message;
        return result;
    }

    QHash<QString, AbelBackendFunction> functions;
    for (const auto& function : backend->functions())
        functions.insert(function.symbol, function);

    for (const QString& symbol : node.symbols) {
        const auto found = functions.constFind(symbol);
        if (found == functions.constEnd()) {
            addLoadError(result.diagnostics,
                         QStringLiteral("plugin backend '%1' does not export symbol '%2'").arg(node.backendId, symbol));
            continue;
        }

        const BackendFunctionDesc pluginDesc{
            node.backendId,
            symbol,
            found->returnType,
            found->params,
            found->variadic,
        };
        if (const BackendFunctionDesc* existing = registry.findFunction(node.backendId, symbol)) {
            if (!sameSignature(*existing, *found)) {
                addLoadError(result.diagnostics,
                             QStringLiteral("plugin symbol '%1' signature does not match registry declaration").arg(symbol));
                continue;
            }
        } else {
            Diagnostic diagnostic;
            if (!registry.registerFunction(pluginDesc, &diagnostic)) {
                result.diagnostics.push_back(diagnostic);
                continue;
            }
        }

        Diagnostic diagnostic;
        if (!registry.bindFunction(node.backendId,
                                   symbol,
                                   [backend, loader, symbol](const BackendCall& call, AbelRuntimeContext& ctx) {
                                       QList<AbelValue> args;
                                       args.reserve(static_cast<qsizetype>(call.args.size()));
                                       for (size_t i = 0; i < call.args.size(); ++i) {
                                           if (i < call.argLocations.size() && call.argLocations[i])
                                               args.push_back(call.argLocations[i]->read());
                                           else
                                               args.push_back(call.args[i]);
                                       }
                                       return backend->call(symbol, args, ctx);
                                   },
                                   &diagnostic)) {
            result.diagnostics.push_back(diagnostic);
        }
    }

    if (!result.diagnostics.isEmpty()) {
        result.node.lastError = result.diagnostics.back().message;
        return result;
    }

    result.node.state = ResourceNodeState::Loaded;
    result.node.lastError.clear();
    result.loader = std::move(loader);
    return result;
}

} // namespace abel
