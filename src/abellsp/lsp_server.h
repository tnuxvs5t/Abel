#pragma once

#include "lsp_analyzer.h"

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>

namespace abel::lsp {

class Server {
public:
    int run();
    QJsonObject handleMessage(const QJsonObject& message);

private:
    Analyzer m_analyzer;
    QString m_workspaceRoot;
    QHash<QString, QString> m_openDocuments;
    QSet<QString> m_lastPublishedFiles;
    bool m_shutdown = false;

    void send(const QJsonObject& message) const;
    void sendResponse(const QJsonValue& id, const QJsonValue& result);
    void sendNotification(const QString& method, const QJsonObject& params) const;

    QJsonObject handleRequest(const QJsonObject& message, const QString& method);
    void handleNotification(const QJsonObject& message, const QString& method);

    void analyzeAndPublish(const QString& filePath);
    QJsonArray completionItems(const QString& filePath = {}) const;
};

} // namespace abel::lsp
