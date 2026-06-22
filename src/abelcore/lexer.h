#pragma once

#include "abelcore/diagnostic.h"
#include "abelcore/token.h"

#include <QList>
#include <QString>

namespace abel {

struct LexResult {
    QList<Token> tokens;
    QList<Diagnostic> diagnostics;
};

class Lexer {
public:
    LexResult lex(const QString& fileName, const QString& source);

private:
    QString m_fileName;
    QString m_source;
    qsizetype m_pos = 0;
    int m_line = 1;
    int m_column = 1;
    QList<Token> m_tokens;
    QList<Diagnostic> m_diagnostics;

    bool atEnd() const;
    QChar peek(int offset = 0) const;
    QChar advance();
    bool match(QChar ch);
    SourceSpan startSpan(qsizetype startPos, int startLine, int startColumn) const;
    void add(TokenKind kind, const QString& text, qsizetype startPos, int startLine, int startColumn);
    void error(const QString& message, qsizetype startPos, int startLine, int startColumn);
    void skipWhitespaceAndComments();
    void lexToken();
    void lexIdentifierOrKeyword(qsizetype startPos, int startLine, int startColumn);
    void lexNumber(qsizetype startPos, int startLine, int startColumn);
    void lexString(qsizetype startPos, int startLine, int startColumn);
    void lexChar(qsizetype startPos, int startLine, int startColumn);
};

} // namespace abel
