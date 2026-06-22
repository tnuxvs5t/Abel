#pragma once

#include "abelcore/backend_registry.h"
#include "abelcore/diagnostic.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <memory>

class QPluginLoader;

namespace abel {

enum class ResourceNodeState {
    Unloaded,
    Loaded,
    Failed,
};

struct ResourceNode {
    QString id;
    QString kind;
    QString path;
    QString iid;
    QString backendId;
    QString qtVersion;
    QString kit;
    QStringList symbols;
    ResourceNodeState state = ResourceNodeState::Unloaded;
    QString lastError;
};

QString resourceNodeStateName(ResourceNodeState state);
ResourceNodeState resourceNodeStateFromName(const QString& name, bool* ok = nullptr);

struct ResourceNodeParseResult {
    ResourceNode node;
    QList<Diagnostic> diagnostics;
};

ResourceNodeParseResult resourceNodeFromJson(const QJsonObject& object, const SourceSpan& span = {});
ResourceNodeParseResult resourceNodeFromJsonText(const QString& text, const QString& file = {});

class ResourceNodeLoadResult {
public:
    ResourceNodeLoadResult();
    ~ResourceNodeLoadResult();

    ResourceNodeLoadResult(ResourceNodeLoadResult&&) noexcept;
    ResourceNodeLoadResult& operator=(ResourceNodeLoadResult&&) noexcept;

    ResourceNodeLoadResult(const ResourceNodeLoadResult&) = delete;
    ResourceNodeLoadResult& operator=(const ResourceNodeLoadResult&) = delete;

    bool ok() const { return diagnostics.isEmpty() && node.state == ResourceNodeState::Loaded; }

    ResourceNode node;
    QList<Diagnostic> diagnostics;

private:
    friend ResourceNodeLoadResult loadBackendResourceNode(const ResourceNode& node,
                                                          BackendRegistry& registry,
                                                          const QString& baseDir);

    std::shared_ptr<QPluginLoader> loader;
};

ResourceNodeLoadResult loadBackendResourceNode(const ResourceNode& node,
                                               BackendRegistry& registry,
                                               const QString& baseDir = {});

} // namespace abel
