#include "abelcore/resource_node.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

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

QString requiredString(const QJsonObject& object, const QString& key, QList<Diagnostic>& diagnostics, const SourceSpan& span)
{
    const QJsonValue value = object.value(key);
    if (!value.isString() || value.toString().isEmpty()) {
        addError(diagnostics, QStringLiteral("resource node requires non-empty string field '%1'").arg(key), span);
        return {};
    }
    return value.toString();
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

} // namespace abel
