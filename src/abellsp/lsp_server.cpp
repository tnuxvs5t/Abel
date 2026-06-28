#include "lsp_server.h"

#include "lsp_protocol.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

#include <iostream>
#include <optional>
#include <string>

namespace abel::lsp {
namespace {

std::optional<QByteArray> readMessageBody()
{
    std::string line;
    int contentLength = -1;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            break;
        const std::string prefix = "Content-Length:";
        if (line.rfind(prefix, 0) == 0) {
            const std::string value = line.substr(prefix.size());
            contentLength = std::stoi(value);
        }
    }
    if (contentLength < 0)
        return std::nullopt;

    QByteArray body;
    body.resize(contentLength);
    std::cin.read(body.data(), contentLength);
    if (std::cin.gcount() != contentLength)
        return std::nullopt;
    return body;
}

QJsonObject nullResultCapabilities()
{
    QJsonObject capabilities;
    capabilities.insert(QStringLiteral("textDocumentSync"), 1);
    capabilities.insert(QStringLiteral("documentSymbolProvider"), true);
    capabilities.insert(QStringLiteral("hoverProvider"), true);
    capabilities.insert(QStringLiteral("definitionProvider"), true);
    capabilities.insert(QStringLiteral("workspaceSymbolProvider"), true);
    capabilities.insert(QStringLiteral("referencesProvider"), true);
    capabilities.insert(QStringLiteral("documentHighlightProvider"), true);
    capabilities.insert(QStringLiteral("foldingRangeProvider"), true);

    QJsonObject semanticTokensProvider;
    QJsonObject legend;
    QJsonArray tokenTypes;
    for (const QString& type : {QStringLiteral("keyword"),
                                QStringLiteral("type"),
                                QStringLiteral("function"),
                                QStringLiteral("variable"),
                                QStringLiteral("property"),
                                QStringLiteral("string"),
                                QStringLiteral("number"),
                                QStringLiteral("operator")}) {
        tokenTypes.push_back(type);
    }
    legend.insert(QStringLiteral("tokenTypes"), tokenTypes);
    legend.insert(QStringLiteral("tokenModifiers"), QJsonArray());
    semanticTokensProvider.insert(QStringLiteral("legend"), legend);
    semanticTokensProvider.insert(QStringLiteral("full"), true);
    capabilities.insert(QStringLiteral("semanticTokensProvider"), semanticTokensProvider);

    QJsonObject completionProvider;
    completionProvider.insert(QStringLiteral("resolveProvider"), false);
    capabilities.insert(QStringLiteral("completionProvider"), completionProvider);

    QJsonObject signatureHelpProvider;
    QJsonArray triggerCharacters;
    triggerCharacters.push_back(QStringLiteral("("));
    triggerCharacters.push_back(QStringLiteral(","));
    signatureHelpProvider.insert(QStringLiteral("triggerCharacters"), triggerCharacters);
    capabilities.insert(QStringLiteral("signatureHelpProvider"), signatureHelpProvider);
    return capabilities;
}

QJsonObject makeCompletionItem(const QString& label, int kind, const QString& insertText = {})
{
    QJsonObject item;
    item.insert(QStringLiteral("label"), label);
    item.insert(QStringLiteral("kind"), kind);
    if (!insertText.isEmpty()) {
        item.insert(QStringLiteral("insertText"), insertText);
        item.insert(QStringLiteral("insertTextFormat"), 2);
    }
    return item;
}

} // namespace

int Server::run()
{
    while (true) {
        auto body = readMessageBody();
        if (!body.has_value())
            break;

        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(*body, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject())
            continue;

        const QJsonObject response = handleMessage(doc.object());
        if (!response.isEmpty())
            send(response);
        if (m_shutdown && doc.object().value(QStringLiteral("method")).toString() == QStringLiteral("exit"))
            break;
    }
    return 0;
}

QJsonObject Server::handleMessage(const QJsonObject& message)
{
    const QString method = message.value(QStringLiteral("method")).toString();
    if (message.contains(QStringLiteral("id")))
        return handleRequest(message, method);
    handleNotification(message, method);
    return {};
}

void Server::send(const QJsonObject& message) const
{
    const QByteArray encoded = encodeMessage(message);
    std::cout.write(encoded.constData(), encoded.size());
    std::cout.flush();
}

void Server::sendResponse(const QJsonValue& id, const QJsonValue& result)
{
    QJsonObject response;
    response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    response.insert(QStringLiteral("id"), id);
    response.insert(QStringLiteral("result"), result);
    send(response);
}

void Server::sendNotification(const QString& method, const QJsonObject& params) const
{
    QJsonObject notification;
    notification.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    notification.insert(QStringLiteral("method"), method);
    notification.insert(QStringLiteral("params"), params);
    send(notification);
}

QJsonObject Server::handleRequest(const QJsonObject& message, const QString& method)
{
    const QJsonValue id = message.value(QStringLiteral("id"));
    const QJsonObject params = message.value(QStringLiteral("params")).toObject();

    QJsonValue result;
    if (method == QStringLiteral("initialize")) {
        const QString rootUri = params.value(QStringLiteral("rootUri")).toString();
        const QString rootPath = params.value(QStringLiteral("rootPath")).toString();
        if (!rootUri.isEmpty())
            m_workspaceRoot = pathFromUri(rootUri);
        else if (!rootPath.isEmpty())
            m_workspaceRoot = QFileInfo(rootPath).absoluteFilePath();

        QJsonArray folders = params.value(QStringLiteral("workspaceFolders")).toArray();
        if (m_workspaceRoot.isEmpty() && !folders.isEmpty())
            m_workspaceRoot = pathFromUri(folders.first().toObject().value(QStringLiteral("uri")).toString());

        QJsonObject init;
        init.insert(QStringLiteral("capabilities"), nullResultCapabilities());
        result = init;
    } else if (method == QStringLiteral("shutdown")) {
        m_shutdown = true;
        result = QJsonValue();
    } else if (method == QStringLiteral("textDocument/documentSymbol")) {
        const QString path = pathFromUri(params.value(QStringLiteral("textDocument")).toObject().value(QStringLiteral("uri")).toString());
        AnalyzerResult analyzed = m_analyzer.analyzeFile(path, m_openDocuments, m_workspaceRoot);
        result = analyzed.documentSymbols.value(QFileInfo(path).absoluteFilePath());
    } else if (method == QStringLiteral("textDocument/hover")) {
        const QString path = pathFromUri(params.value(QStringLiteral("textDocument")).toObject().value(QStringLiteral("uri")).toString());
        const QJsonObject position = params.value(QStringLiteral("position")).toObject();
        const QJsonObject hover = m_analyzer.hover(path,
                                                   position.value(QStringLiteral("line")).toInt(),
                                                   position.value(QStringLiteral("character")).toInt(),
                                                   m_openDocuments,
                                                   m_workspaceRoot);
        result = hover.isEmpty() ? QJsonValue() : QJsonValue(hover);
    } else if (method == QStringLiteral("textDocument/definition")) {
        const QString path = pathFromUri(params.value(QStringLiteral("textDocument")).toObject().value(QStringLiteral("uri")).toString());
        const QJsonObject position = params.value(QStringLiteral("position")).toObject();
        result = m_analyzer.definitions(path,
                                        position.value(QStringLiteral("line")).toInt(),
                                        position.value(QStringLiteral("character")).toInt(),
                                        m_openDocuments,
                                        m_workspaceRoot);
    } else if (method == QStringLiteral("textDocument/references")) {
        const QString path = pathFromUri(params.value(QStringLiteral("textDocument")).toObject().value(QStringLiteral("uri")).toString());
        const QJsonObject position = params.value(QStringLiteral("position")).toObject();
        result = m_analyzer.references(path,
                                       position.value(QStringLiteral("line")).toInt(),
                                       position.value(QStringLiteral("character")).toInt(),
                                       m_openDocuments,
                                       m_workspaceRoot);
    } else if (method == QStringLiteral("textDocument/documentHighlight")) {
        const QString path = pathFromUri(params.value(QStringLiteral("textDocument")).toObject().value(QStringLiteral("uri")).toString());
        const QJsonObject position = params.value(QStringLiteral("position")).toObject();
        result = m_analyzer.documentHighlights(path,
                                               position.value(QStringLiteral("line")).toInt(),
                                               position.value(QStringLiteral("character")).toInt(),
                                               m_openDocuments,
                                               m_workspaceRoot);
    } else if (method == QStringLiteral("workspace/symbol")) {
        result = m_analyzer.workspaceSymbols(params.value(QStringLiteral("query")).toString(),
                                             m_workspaceRoot,
                                             m_openDocuments);
    } else if (method == QStringLiteral("textDocument/completion")) {
        const QString path = pathFromUri(params.value(QStringLiteral("textDocument")).toObject().value(QStringLiteral("uri")).toString());
        const QJsonObject position = params.value(QStringLiteral("position")).toObject();
        result = completionItems(path,
                                 position.value(QStringLiteral("line")).toInt(-1),
                                 position.value(QStringLiteral("character")).toInt(-1));
    } else if (method == QStringLiteral("textDocument/signatureHelp")) {
        const QString path = pathFromUri(params.value(QStringLiteral("textDocument")).toObject().value(QStringLiteral("uri")).toString());
        const QJsonObject position = params.value(QStringLiteral("position")).toObject();
        const QJsonObject help = m_analyzer.signatureHelp(path,
                                                          position.value(QStringLiteral("line")).toInt(),
                                                          position.value(QStringLiteral("character")).toInt(),
                                                          m_openDocuments,
                                                          m_workspaceRoot);
        result = help.isEmpty() ? QJsonValue() : QJsonValue(help);
    } else if (method == QStringLiteral("textDocument/foldingRange")) {
        const QString path = pathFromUri(params.value(QStringLiteral("textDocument")).toObject().value(QStringLiteral("uri")).toString());
        result = m_analyzer.foldingRanges(path, m_openDocuments, m_workspaceRoot);
    } else if (method == QStringLiteral("textDocument/semanticTokens/full")) {
        const QString path = pathFromUri(params.value(QStringLiteral("textDocument")).toObject().value(QStringLiteral("uri")).toString());
        result = m_analyzer.semanticTokens(path, m_openDocuments, m_workspaceRoot);
    } else {
        result = QJsonValue();
    }

    QJsonObject response;
    response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    response.insert(QStringLiteral("id"), id);
    response.insert(QStringLiteral("result"), result);
    return response;
}

void Server::handleNotification(const QJsonObject& message, const QString& method)
{
    const QJsonObject params = message.value(QStringLiteral("params")).toObject();
    if (method == QStringLiteral("initialized")) {
        return;
    }
    if (method == QStringLiteral("exit")) {
        m_shutdown = true;
        return;
    }

    if (method == QStringLiteral("textDocument/didOpen")) {
        const QJsonObject textDocument = params.value(QStringLiteral("textDocument")).toObject();
        const QString path = pathFromUri(textDocument.value(QStringLiteral("uri")).toString());
        m_openDocuments.insert(QFileInfo(path).absoluteFilePath(), textDocument.value(QStringLiteral("text")).toString());
        analyzeAndPublish(path);
    } else if (method == QStringLiteral("textDocument/didChange")) {
        const QJsonObject textDocument = params.value(QStringLiteral("textDocument")).toObject();
        const QString path = pathFromUri(textDocument.value(QStringLiteral("uri")).toString());
        const QJsonArray changes = params.value(QStringLiteral("contentChanges")).toArray();
        if (!changes.isEmpty())
            m_openDocuments.insert(QFileInfo(path).absoluteFilePath(), changes.last().toObject().value(QStringLiteral("text")).toString());
        analyzeAndPublish(path);
    } else if (method == QStringLiteral("textDocument/didSave")) {
        const QJsonObject textDocument = params.value(QStringLiteral("textDocument")).toObject();
        const QString path = pathFromUri(textDocument.value(QStringLiteral("uri")).toString());
        if (params.contains(QStringLiteral("text")))
            m_openDocuments.insert(QFileInfo(path).absoluteFilePath(), params.value(QStringLiteral("text")).toString());
        analyzeAndPublish(path);
    } else if (method == QStringLiteral("textDocument/didClose")) {
        const QJsonObject textDocument = params.value(QStringLiteral("textDocument")).toObject();
        const QString path = QFileInfo(pathFromUri(textDocument.value(QStringLiteral("uri")).toString())).absoluteFilePath();
        m_openDocuments.remove(path);
        QJsonObject clear;
        clear.insert(QStringLiteral("uri"), uriFromPath(path));
        clear.insert(QStringLiteral("diagnostics"), QJsonArray());
        sendNotification(QStringLiteral("textDocument/publishDiagnostics"), clear);
        m_lastPublishedFiles.remove(path);
    }
}

void Server::analyzeAndPublish(const QString& filePath)
{
    AnalyzerResult analyzed = m_analyzer.analyzeFile(filePath, m_openDocuments, m_workspaceRoot);
    QHash<QString, QJsonArray> byFile;
    for (const QString& file : analyzed.analyzedFiles)
        byFile.insert(file, QJsonArray());

    for (const Diagnostic& diagnostic : analyzed.diagnostics) {
        QString file = QFileInfo(diagnostic.primary.file).absoluteFilePath();
        if (file.isEmpty() || diagnostic.primary.file.isEmpty())
            file = QFileInfo(filePath).absoluteFilePath();
        byFile[file].push_back(diagnosticToLsp(diagnostic));
    }

    QSet<QString> publishFiles = analyzed.analyzedFiles;
    publishFiles.unite(m_lastPublishedFiles);
    for (const QString& file : publishFiles) {
        QJsonObject params;
        params.insert(QStringLiteral("uri"), uriFromPath(file));
        params.insert(QStringLiteral("diagnostics"), byFile.value(file));
        sendNotification(QStringLiteral("textDocument/publishDiagnostics"), params);
    }
    m_lastPublishedFiles = analyzed.analyzedFiles;
}

QJsonArray Server::completionItems(const QString& filePath, int zeroBasedLine, int zeroBasedCharacter) const
{
    QJsonArray items;
    const QStringList keywords = {
        QStringLiteral("module"), QStringLiteral("use"), QStringLiteral("export"),
        QStringLiteral("fn"), QStringLiteral("struct"), QStringLiteral("backend"),
        QStringLiteral("return"), QStringLiteral("if"), QStringLiteral("else"),
        QStringLiteral("while"), QStringLiteral("for"), QStringLiteral("repeat"),
        QStringLiteral("break"), QStringLiteral("continue"), QStringLiteral("const"),
        QStringLiteral("public"), QStringLiteral("private"), QStringLiteral("any"),
        QStringLiteral("bool"), QStringLiteral("int"), QStringLiteral("long"),
        QStringLiteral("i64"), QStringLiteral("f64"), QStringLiteral("char"),
        QStringLiteral("str"), QStringLiteral("void"), QStringLiteral("vector"),
        QStringLiteral("func"), QStringLiteral("cast"), QStringLiteral("any_type"),
        QStringLiteral("any_is"), QStringLiteral("any_debug"),
    };
    for (const QString& keyword : keywords)
        items.push_back(makeCompletionItem(keyword, 14));

    items.push_back(makeCompletionItem(QStringLiteral("main function"),
                                       15,
                                       QStringLiteral("fn int main() {\n    return 0;\n}")));
    if (!filePath.isEmpty()) {
        const QJsonArray symbolItems = zeroBasedLine >= 0 && zeroBasedCharacter >= 0
            ? m_analyzer.completionItems(filePath, zeroBasedLine, zeroBasedCharacter, m_openDocuments, m_workspaceRoot)
            : m_analyzer.completionItems(filePath, m_openDocuments, m_workspaceRoot);
        for (const QJsonValue& item : symbolItems)
            items.push_back(item);
    }
    return items;
}

} // namespace abel::lsp
