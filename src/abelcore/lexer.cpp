#include "abelcore/lexer.h"

#include <QHash>

namespace abel {

QString tokenKindName(TokenKind kind)
{
    switch (kind) {
    case TokenKind::End: return QStringLiteral("end");
    case TokenKind::Identifier: return QStringLiteral("identifier");
    case TokenKind::Integer: return QStringLiteral("integer");
    case TokenKind::Float: return QStringLiteral("float");
    case TokenKind::String: return QStringLiteral("string");
    case TokenKind::Char: return QStringLiteral("char");
    default: return QString::number(static_cast<int>(kind));
    }
}

LexResult Lexer::lex(const QString& fileName, const QString& source)
{
    m_fileName = fileName;
    m_source = source;
    m_pos = 0;
    m_line = 1;
    m_column = 1;
    m_tokens.clear();
    m_diagnostics.clear();

    while (!atEnd()) {
        skipWhitespaceAndComments();
        if (!atEnd())
            lexToken();
    }

    SourceSpan span;
    span.file = m_fileName;
    span.startOffset = span.endOffset = static_cast<int>(m_pos);
    span.startLine = span.endLine = m_line;
    span.startColumn = span.endColumn = m_column;
    m_tokens.push_back(Token{TokenKind::End, QString(), span});
    return {m_tokens, m_diagnostics};
}

bool Lexer::atEnd() const { return m_pos >= m_source.size(); }

QChar Lexer::peek(int offset) const
{
    const qsizetype p = m_pos + offset;
    if (p < 0 || p >= m_source.size())
        return QChar();
    return m_source[p];
}

QChar Lexer::advance()
{
    const QChar ch = peek();
    ++m_pos;
    if (ch == QChar('\n')) {
        ++m_line;
        m_column = 1;
    } else {
        ++m_column;
    }
    return ch;
}

bool Lexer::match(QChar ch)
{
    if (peek() != ch)
        return false;
    advance();
    return true;
}

QString Lexer::sourceLineAt(qsizetype pos) const
{
    if (pos < 0 || pos >= m_source.size())
        return {};

    qsizetype begin = pos;
    while (begin > 0 && m_source[begin - 1] != QChar('\n') && m_source[begin - 1] != QChar('\r'))
        --begin;

    qsizetype end = pos;
    while (end < m_source.size() && m_source[end] != QChar('\n') && m_source[end] != QChar('\r'))
        ++end;

    return m_source.mid(begin, end - begin);
}

SourceSpan Lexer::startSpan(qsizetype startPos, int startLine, int startColumn) const
{
    SourceSpan span;
    span.file = m_fileName;
    span.sourceLine = sourceLineAt(startPos);
    span.startOffset = static_cast<int>(startPos);
    span.endOffset = static_cast<int>(m_pos);
    span.startLine = startLine;
    span.startColumn = startColumn;
    span.endLine = m_line;
    span.endColumn = m_column;
    return span;
}

void Lexer::add(TokenKind kind, const QString& text, qsizetype startPos, int startLine, int startColumn)
{
    m_tokens.push_back(Token{kind, text, startSpan(startPos, startLine, startColumn)});
}

void Lexer::error(const QString& message, qsizetype startPos, int startLine, int startColumn)
{
    Diagnostic d;
    d.severity = Severity::Error;
    d.code = QStringLiteral("E0101");
    d.message = message;
    d.primary = startSpan(startPos, startLine, startColumn);
    m_diagnostics.push_back(d);
}

void Lexer::skipWhitespaceAndComments()
{
    for (;;) {
        while (!atEnd() && peek().isSpace())
            advance();
        if (peek() == QChar('/') && peek(1) == QChar('/')) {
            while (!atEnd() && peek() != QChar('\n'))
                advance();
            continue;
        }
        if (peek() == QChar('/') && peek(1) == QChar('*')) {
            advance();
            advance();
            while (!atEnd() && !(peek() == QChar('*') && peek(1) == QChar('/')))
                advance();
            if (!atEnd()) {
                advance();
                advance();
            }
            continue;
        }
        break;
    }
}

static bool isIdentStart(QChar ch)
{
    return ch == QChar('_') || ch.isLetter();
}

static bool isIdentPart(QChar ch)
{
    return ch == QChar('_') || ch.isLetterOrNumber();
}

void Lexer::lexToken()
{
    const qsizetype startPos = m_pos;
    const int startLine = m_line;
    const int startColumn = m_column;
    const QChar ch = advance();

    if (isIdentStart(ch)) {
        lexIdentifierOrKeyword(startPos, startLine, startColumn);
        return;
    }
    if (ch.isDigit()) {
        lexNumber(startPos, startLine, startColumn);
        return;
    }

    switch (ch.unicode()) {
    case '(': add(TokenKind::LParen, QStringLiteral("("), startPos, startLine, startColumn); return;
    case ')': add(TokenKind::RParen, QStringLiteral(")"), startPos, startLine, startColumn); return;
    case '{': add(TokenKind::LBrace, QStringLiteral("{"), startPos, startLine, startColumn); return;
    case '}': add(TokenKind::RBrace, QStringLiteral("}"), startPos, startLine, startColumn); return;
    case '[': add(TokenKind::LBracket, QStringLiteral("["), startPos, startLine, startColumn); return;
    case ']': add(TokenKind::RBracket, QStringLiteral("]"), startPos, startLine, startColumn); return;
    case ',': add(TokenKind::Comma, QStringLiteral(","), startPos, startLine, startColumn); return;
    case ';': add(TokenKind::Semicolon, QStringLiteral(";"), startPos, startLine, startColumn); return;
    case ':':
        if (match(QChar(':'))) add(TokenKind::Scope, QStringLiteral("::"), startPos, startLine, startColumn);
        else add(TokenKind::Colon, QStringLiteral(":"), startPos, startLine, startColumn);
        return;
    case '.':
        if (match(QChar('.')) && match(QChar('.'))) add(TokenKind::Ellipsis, QStringLiteral("..."), startPos, startLine, startColumn);
        else add(TokenKind::Dot, QStringLiteral("."), startPos, startLine, startColumn);
        return;
    case '+': add(TokenKind::Plus, QStringLiteral("+"), startPos, startLine, startColumn); return;
    case '-':
        if (match(QChar('>'))) add(TokenKind::Arrow, QStringLiteral("->"), startPos, startLine, startColumn);
        else add(TokenKind::Minus, QStringLiteral("-"), startPos, startLine, startColumn);
        return;
    case '*':
        if (match(QChar('*'))) add(TokenKind::Power, QStringLiteral("**"), startPos, startLine, startColumn);
        else add(TokenKind::Star, QStringLiteral("*"), startPos, startLine, startColumn);
        return;
    case '/': add(TokenKind::Slash, QStringLiteral("/"), startPos, startLine, startColumn); return;
    case '%':
        if (match(QChar('%'))) add(TokenKind::ModMod, QStringLiteral("%%"), startPos, startLine, startColumn);
        else add(TokenKind::Percent, QStringLiteral("%"), startPos, startLine, startColumn);
        return;
    case '&':
        if (match(QChar('&'))) add(TokenKind::AndAnd, QStringLiteral("&&"), startPos, startLine, startColumn);
        else add(TokenKind::Amp, QStringLiteral("&"), startPos, startLine, startColumn);
        return;
    case '|':
        if (match(QChar('|'))) add(TokenKind::OrOr, QStringLiteral("||"), startPos, startLine, startColumn);
        else if (match(QChar('>'))) add(TokenKind::PipeForward, QStringLiteral("|>"), startPos, startLine, startColumn);
        else add(TokenKind::Pipe, QStringLiteral("|"), startPos, startLine, startColumn);
        return;
    case '!':
        if (match(QChar('='))) add(TokenKind::BangEqual, QStringLiteral("!="), startPos, startLine, startColumn);
        else add(TokenKind::Bang, QStringLiteral("!"), startPos, startLine, startColumn);
        return;
    case '=':
        if (match(QChar('='))) add(TokenKind::EqualEqual, QStringLiteral("=="), startPos, startLine, startColumn);
        else add(TokenKind::Equal, QStringLiteral("="), startPos, startLine, startColumn);
        return;
    case '<':
        if (match(QChar('='))) add(TokenKind::LessEqual, QStringLiteral("<="), startPos, startLine, startColumn);
        else if (match(QChar('?'))) add(TokenKind::MinOp, QStringLiteral("<?"), startPos, startLine, startColumn);
        else add(TokenKind::Less, QStringLiteral("<"), startPos, startLine, startColumn);
        return;
    case '>':
        if (match(QChar('='))) add(TokenKind::GreaterEqual, QStringLiteral(">="), startPos, startLine, startColumn);
        else if (match(QChar('?'))) add(TokenKind::MaxOp, QStringLiteral(">?"), startPos, startLine, startColumn);
        else add(TokenKind::Greater, QStringLiteral(">"), startPos, startLine, startColumn);
        return;
    case '"': lexString(startPos, startLine, startColumn); return;
    case '\'': lexChar(startPos, startLine, startColumn); return;
    default:
        error(QStringLiteral("unexpected character '%1'").arg(ch), startPos, startLine, startColumn);
        return;
    }
}

void Lexer::lexIdentifierOrKeyword(qsizetype startPos, int startLine, int startColumn)
{
    while (isIdentPart(peek()))
        advance();
    const QString text = m_source.mid(startPos, m_pos - startPos);
    static const QHash<QString, TokenKind> keywords = {
        {QStringLiteral("fn"), TokenKind::KwFn},
        {QStringLiteral("return"), TokenKind::KwReturn},
        {QStringLiteral("if"), TokenKind::KwIf},
        {QStringLiteral("elseif"), TokenKind::KwElseif},
        {QStringLiteral("else"), TokenKind::KwElse},
        {QStringLiteral("while"), TokenKind::KwWhile},
        {QStringLiteral("for"), TokenKind::KwFor},
        {QStringLiteral("in"), TokenKind::KwIn},
        {QStringLiteral("repeat"), TokenKind::KwRepeat},
        {QStringLiteral("break"), TokenKind::KwBreak},
        {QStringLiteral("continue"), TokenKind::KwContinue},
        {QStringLiteral("true"), TokenKind::KwTrue},
        {QStringLiteral("false"), TokenKind::KwFalse},
        {QStringLiteral("nullptr"), TokenKind::KwNullptr},
        {QStringLiteral("const"), TokenKind::KwConst},
        {QStringLiteral("struct"), TokenKind::KwStruct},
        {QStringLiteral("init"), TokenKind::KwInit},
        {QStringLiteral("backend"), TokenKind::KwBackend},
        {QStringLiteral("debt"), TokenKind::KwDebt},
        {QStringLiteral("lambda"), TokenKind::KwLambda},
        {QStringLiteral("vector"), TokenKind::KwVector},
        {QStringLiteral("func"), TokenKind::KwFunc},
        {QStringLiteral("any"), TokenKind::KwAny},
        {QStringLiteral("new"), TokenKind::KwNew},
        {QStringLiteral("delete"), TokenKind::KwDelete},
        {QStringLiteral("cast"), TokenKind::KwCast},
        {QStringLiteral("this"), TokenKind::KwThis},
        {QStringLiteral("enum"), TokenKind::KwEnum},
        {QStringLiteral("type"), TokenKind::KwType},
        {QStringLiteral("export"), TokenKind::KwExport},
        {QStringLiteral("module"), TokenKind::KwModule},
        {QStringLiteral("use"), TokenKind::KwUse},
        {QStringLiteral("as"), TokenKind::KwAs},
        {QStringLiteral("public"), TokenKind::KwPublic},
        {QStringLiteral("private"), TokenKind::KwPrivate},
        {QStringLiteral("operator"), TokenKind::KwOperator},
        {QStringLiteral("template"), TokenKind::KwTemplate},
        {QStringLiteral("interface"), TokenKind::KwInterface},
        {QStringLiteral("require"), TokenKind::KwRequire},
    };
    add(keywords.value(text, TokenKind::Identifier), text, startPos, startLine, startColumn);
}

void Lexer::lexNumber(qsizetype startPos, int startLine, int startColumn)
{
    while (peek().isDigit())
        advance();
    bool isFloat = false;
    if (peek() == QChar('.') && peek(1).isDigit()) {
        isFloat = true;
        advance();
        while (peek().isDigit())
            advance();
    }
    add(isFloat ? TokenKind::Float : TokenKind::Integer,
        m_source.mid(startPos, m_pos - startPos),
        startPos, startLine, startColumn);
}

void Lexer::lexString(qsizetype startPos, int startLine, int startColumn)
{
    QString text;
    while (!atEnd() && peek() != QChar('"')) {
        QChar ch = advance();
        if (ch == QChar('\\')) {
            const QChar esc = advance();
            if (esc == QChar('n')) text.push_back(QChar('\n'));
            else if (esc == QChar('t')) text.push_back(QChar('\t'));
            else text.push_back(esc);
        } else {
            text.push_back(ch);
        }
    }
    if (atEnd()) {
        error(QStringLiteral("unterminated string literal"), startPos, startLine, startColumn);
        return;
    }
    advance();
    add(TokenKind::String, text, startPos, startLine, startColumn);
}

void Lexer::lexChar(qsizetype startPos, int startLine, int startColumn)
{
    QString text;
    if (!atEnd()) {
        QChar ch = advance();
        if (ch == QChar('\\')) {
            const QChar esc = advance();
            if (esc == QChar('n')) text.push_back(QChar('\n'));
            else if (esc == QChar('t')) text.push_back(QChar('\t'));
            else text.push_back(esc);
        } else {
            text.push_back(ch);
        }
    }
    if (!match(QChar('\''))) {
        error(QStringLiteral("unterminated char literal"), startPos, startLine, startColumn);
        return;
    }
    add(TokenKind::Char, text, startPos, startLine, startColumn);
}

} // namespace abel
