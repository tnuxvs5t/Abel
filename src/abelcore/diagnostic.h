#pragma once

#include "abelcore/source_span.h"

#include <QString>
#include <QStringList>
#include <QList>

namespace abel {

enum class Severity {
    Error,
    Warning,
    Note
};

struct Diagnostic {
    Severity severity = Severity::Error;
    QString code;
    QString message;
    SourceSpan primary;
    QList<SourceSpan> related;
    QString explanation;
    QStringList suggestions;
};

} // namespace abel

