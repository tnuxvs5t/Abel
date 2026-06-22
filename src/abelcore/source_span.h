#pragma once

#include <QString>

namespace abel {

struct SourceSpan {
    QString file;
    int startOffset = 0;
    int endOffset = 0;
    int startLine = 1;
    int startColumn = 1;
    int endLine = 1;
    int endColumn = 1;
};

} // namespace abel

