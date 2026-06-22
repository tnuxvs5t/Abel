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

struct DiagnosticStackFrame {
    QString symbol;
    SourceSpan callSite;
};

struct Diagnostic {
    Severity severity = Severity::Error;
    QString code;
    QString message;
    SourceSpan primary;
    QList<SourceSpan> related;
    QList<DiagnosticStackFrame> stackTrace;
    QString explanation;
    QStringList suggestions;
};

} // namespace abel
