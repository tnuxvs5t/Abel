#include "lsp_protocol.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QUrl>

namespace abel::lsp {

QString pathFromUri(const QString& uri)
{
    const QUrl url(uri);
    if (url.isLocalFile())
        return QFileInfo(url.toLocalFile()).absoluteFilePath();
    return QFileInfo(uri).absoluteFilePath();
}

QString uriFromPath(const QString& path)
{
    return QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()).toString();
}

QJsonObject positionFromLineColumn(int oneBasedLine, int oneBasedColumn)
{
    QJsonObject position;
    position.insert(QStringLiteral("line"), qMax(0, oneBasedLine - 1));
    position.insert(QStringLiteral("character"), qMax(0, oneBasedColumn - 1));
    return position;
}

QJsonObject rangeFromSpan(const SourceSpan& span)
{
    QJsonObject range;
    range.insert(QStringLiteral("start"), positionFromLineColumn(span.startLine, span.startColumn));
    range.insert(QStringLiteral("end"), positionFromLineColumn(span.endLine, span.endColumn));
    return range;
}

static int severityToLsp(Severity severity)
{
    switch (severity) {
    case Severity::Error:
        return 1;
    case Severity::Warning:
        return 2;
    case Severity::Note:
        return 3;
    }
    return 1;
}

QJsonObject diagnosticToLsp(const Diagnostic& diagnostic)
{
    QJsonObject object;
    object.insert(QStringLiteral("range"), rangeFromSpan(diagnostic.primary));
    object.insert(QStringLiteral("severity"), severityToLsp(diagnostic.severity));
    if (!diagnostic.code.isEmpty())
        object.insert(QStringLiteral("code"), diagnostic.code);
    object.insert(QStringLiteral("source"), QStringLiteral("abel"));
    object.insert(QStringLiteral("message"), diagnostic.message);

    QJsonArray related;
    for (const SourceSpan& span : diagnostic.related) {
        if (span.file.isEmpty())
            continue;
        QJsonObject item;
        QJsonObject location;
        location.insert(QStringLiteral("uri"), uriFromPath(span.file));
        location.insert(QStringLiteral("range"), rangeFromSpan(span));
        item.insert(QStringLiteral("location"), location);
        item.insert(QStringLiteral("message"), QStringLiteral("related location"));
        related.push_back(item);
    }
    if (!related.isEmpty())
        object.insert(QStringLiteral("relatedInformation"), related);

    return object;
}

QByteArray encodeMessage(const QJsonObject& message)
{
    const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    return QByteArrayLiteral("Content-Length: ")
        + QByteArray::number(body.size())
        + QByteArrayLiteral("\r\n\r\n")
        + body;
}

} // namespace abel::lsp
