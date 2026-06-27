#pragma once

#include "abelcore/diagnostic.h"
#include "abelcore/source_span.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace abel::lsp {

QString pathFromUri(const QString& uri);
QString uriFromPath(const QString& path);

QJsonObject positionFromLineColumn(int oneBasedLine, int oneBasedColumn);
QJsonObject rangeFromSpan(const SourceSpan& span);
QJsonObject diagnosticToLsp(const Diagnostic& diagnostic);

QByteArray encodeMessage(const QJsonObject& message);

} // namespace abel::lsp
