#pragma once

#include "abelcore/diagnostic.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

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

} // namespace abel
