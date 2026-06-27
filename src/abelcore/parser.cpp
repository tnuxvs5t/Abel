#include "abelcore/parser.h"

#include <QSet>

namespace abel {

namespace {

SourceSpan mergeSpans(const SourceSpan& first, const SourceSpan& last)
{
    if (first.file.isEmpty())
        return last;
    if (last.file.isEmpty())
        return first;
    SourceSpan out = first;
    out.endOffset = last.endOffset;
    out.endLine = last.endLine;
    out.endColumn = last.endColumn;
    return out;
}

} // namespace

ParseResult Parser::parse(const QList<Token>& tokens)
{
    m_tokens = tokens;
    m_pos = 0;
    m_diagnostics.clear();
    auto program = std::make_unique<ProgramNode>();
    while (!atEnd()) {
        auto decl = parseDeclaration();
        if (decl) {
            program->declarations.push_back(std::move(decl));
        } else if (!atEnd()) {
            synchronizeDeclaration();
        }
    }
    if (!program->declarations.empty())
        program->span = mergeSpans(program->declarations.front()->span, program->declarations.back()->span);
    return {std::move(program), m_diagnostics};
}

const Token& Parser::peek(int offset) const
{
    const qsizetype idx = qMin(m_pos + offset, m_tokens.size() - 1);
    return m_tokens[idx];
}

const Token& Parser::previous() const { return m_tokens[qMax<qsizetype>(0, m_pos - 1)]; }
bool Parser::atEnd() const { return peek().kind == TokenKind::End; }
bool Parser::check(TokenKind kind) const { return peek().kind == kind; }

bool Parser::match(TokenKind kind)
{
    if (!check(kind))
        return false;
    ++m_pos;
    return true;
}

Token Parser::consume(TokenKind kind, const QString& message)
{
    if (check(kind))
        return m_tokens[m_pos++];
    errorAt(peek(), message);
    if (shouldNotConsumeMissing(kind, peek().kind))
        return syntheticToken(kind, peek().span);
    if (!atEnd())
        return m_tokens[m_pos++];
    return syntheticToken(kind, peek().span);
}

Token Parser::syntheticToken(TokenKind kind, const SourceSpan& span) const
{
    return Token{kind, QString(), span};
}

void Parser::errorAt(const Token& token, const QString& message)
{
    errorAtSpan(token.span, message);
}

void Parser::errorAtSpan(const SourceSpan& span, const QString& message)
{
    Diagnostic d;
    d.severity = Severity::Error;
    d.code = QStringLiteral("E0102");
    d.message = message;
    d.primary = span;
    m_diagnostics.push_back(d);
}

bool Parser::shouldNotConsumeMissing(TokenKind expected, TokenKind found) const
{
    if (found == TokenKind::End)
        return true;

    switch (expected) {
    case TokenKind::Semicolon:
        return found == TokenKind::RBrace
            || isStatementStart(found)
            || isTopLevelDeclarationStart(found);
    case TokenKind::RBrace:
        return isTopLevelDeclarationStart(found);
    case TokenKind::RParen:
        return found == TokenKind::LBrace
            || found == TokenKind::Semicolon
            || found == TokenKind::RBrace
            || isStatementStart(found)
            || isTopLevelDeclarationStart(found);
    case TokenKind::RBracket:
        return found == TokenKind::LBrace
            || found == TokenKind::Semicolon
            || found == TokenKind::RBrace
            || isStatementStart(found)
            || isTopLevelDeclarationStart(found);
    case TokenKind::Greater:
        return found == TokenKind::Identifier
            || found == TokenKind::LParen
            || found == TokenKind::Semicolon
            || found == TokenKind::RBrace
            || isStatementStart(found)
            || isTopLevelDeclarationStart(found);
    case TokenKind::LBrace:
        return found == TokenKind::RBrace
            || isStatementStart(found)
            || isTopLevelDeclarationStart(found);
    case TokenKind::LParen:
    case TokenKind::Less:
    case TokenKind::Identifier:
        return found == TokenKind::Semicolon
            || found == TokenKind::RBrace
            || isStatementStart(found)
            || isTopLevelDeclarationStart(found);
    default:
        return false;
    }
}

bool Parser::isTopLevelDeclarationStart(TokenKind kind) const
{
    return kind == TokenKind::KwModule
        || kind == TokenKind::KwUse
        || kind == TokenKind::KwExport
        || kind == TokenKind::KwDebt
        || kind == TokenKind::KwBackend
        || kind == TokenKind::KwStruct
        || kind == TokenKind::KwEnum
        || kind == TokenKind::KwType
        || kind == TokenKind::KwFn;
}

bool Parser::isTopLevelDeclarationStart() const
{
    return isTopLevelDeclarationStart(peek().kind);
}

bool Parser::isStatementStart(TokenKind kind) const
{
    return kind == TokenKind::KwReturn
        || kind == TokenKind::KwIf
        || kind == TokenKind::KwWhile
        || kind == TokenKind::KwFor
        || kind == TokenKind::KwRepeat
        || kind == TokenKind::KwBreak
        || kind == TokenKind::KwContinue
        || kind == TokenKind::LBrace
        || kind == TokenKind::KwConst
        || kind == TokenKind::KwVector
        || kind == TokenKind::KwAny
        || kind == TokenKind::KwFunc
        || kind == TokenKind::KwCast
        || kind == TokenKind::KwLambda
        || kind == TokenKind::KwThis
        || kind == TokenKind::KwTrue
        || kind == TokenKind::KwFalse
        || kind == TokenKind::KwNullptr
        || kind == TokenKind::Identifier
        || kind == TokenKind::Integer
        || kind == TokenKind::Float
        || kind == TokenKind::String
        || kind == TokenKind::Char
        || kind == TokenKind::LParen
        || kind == TokenKind::LBracket
        || kind == TokenKind::Bang
        || kind == TokenKind::Minus
        || kind == TokenKind::Plus
        || kind == TokenKind::Amp
        || kind == TokenKind::Star;
}

void Parser::synchronizeDeclaration()
{
    while (!atEnd()) {
        if (isTopLevelDeclarationStart())
            return;
        ++m_pos;
    }
}

void Parser::synchronizeStatement()
{
    while (!atEnd()) {
        if (previous().kind == TokenKind::Semicolon)
            return;
        if (check(TokenKind::RBrace))
            return;
        if (isStatementStart(peek().kind))
            return;
        ++m_pos;
    }
}

std::unique_ptr<DeclNode> Parser::parseDeclaration()
{
    if (match(TokenKind::KwModule))
        return parseModule();
    if (match(TokenKind::KwUse))
        return parseUse();

    bool exported = match(TokenKind::KwExport);
    if (match(TokenKind::KwUse))
        return parseUse(exported);
    if (match(TokenKind::KwDebt)) {
        consume(TokenKind::KwFn, QStringLiteral("expected fn after debt"));
        return parseFunction(exported, true);
    }
    if (match(TokenKind::KwBackend))
        return parseBackend(exported);
    if (match(TokenKind::KwStruct))
        return parseStruct(exported);
    if (match(TokenKind::KwEnum))
        return parseEnum(exported);
    if (match(TokenKind::KwType))
        return parseTypeAlias(exported);
    if (match(TokenKind::KwFn))
        return parseFunction(exported, false);
    errorAt(peek(), QStringLiteral("expected top-level declaration"));
    return nullptr;
}

std::unique_ptr<ModuleDeclNode> Parser::parseModule()
{
    auto module = std::make_unique<ModuleDeclNode>();
    const SourceSpan startSpan = previous().span;
    module->name = parseQualifiedName(QStringLiteral("module name"));
    const Token semi = consume(TokenKind::Semicolon, QStringLiteral("expected ';' after module declaration"));
    module->span = mergeSpans(startSpan, semi.span);
    return module;
}

std::unique_ptr<UseDeclNode> Parser::parseUse(bool exported)
{
    auto use = std::make_unique<UseDeclNode>();
    const SourceSpan startSpan = previous().span;
    use->exported = exported;
    use->name = parseQualifiedName(QStringLiteral("use target"));
    if (match(TokenKind::KwAs))
        use->alias = consume(TokenKind::Identifier, QStringLiteral("expected alias after as")).text;
    const Token semi = consume(TokenKind::Semicolon, QStringLiteral("expected ';' after use declaration"));
    use->span = mergeSpans(startSpan, semi.span);
    return use;
}

QString Parser::parseQualifiedName(const QString& what)
{
    QStringList parts;
    parts.push_back(consume(TokenKind::Identifier, QStringLiteral("expected %1").arg(what)).text);
    while (match(TokenKind::Dot))
        parts.push_back(consume(TokenKind::Identifier, QStringLiteral("expected identifier after '.' in %1").arg(what)).text);
    return parts.join(QLatin1Char('.'));
}

std::unique_ptr<FunctionDeclNode> Parser::parseFunction(bool exported, bool debt)
{
    auto fn = std::make_unique<FunctionDeclNode>();
    const SourceSpan startSpan = previous().span;
    fn->exported = exported;
    fn->debt = debt;
    fn->returnType = parseType();
    if (match(TokenKind::KwOperator)) {
        fn->isOperator = true;
        fn->operatorSymbol = parseOperatorSymbol();
        fn->name = QStringLiteral("operator ") + fn->operatorSymbol;
    } else {
        const Token name = consume(TokenKind::Identifier, QStringLiteral("expected function name"));
        fn->name = name.text;
    }
    consume(TokenKind::LParen, QStringLiteral("expected '('"));
    if (!check(TokenKind::RParen)) {
        do {
            fn->params.push_back(parseParameter());
        } while (match(TokenKind::Comma));
    }
    consume(TokenKind::RParen, QStringLiteral("expected ')'"));
    bool seenVariadic = false;
    for (size_t i = 0; i < fn->params.size(); ++i) {
        const auto& param = fn->params[i];
        if (!param->variadic)
            continue;
        if (seenVariadic)
            errorAtSpan(param->span, QStringLiteral("only one variadic parameter is allowed"));
        seenVariadic = true;
        if (i + 1 != fn->params.size())
            errorAtSpan(param->span, QStringLiteral("variadic parameter must be last"));
        if (param->type->name != QStringLiteral("any") || param->type->elementType || param->type->pointerDepth > 0 || param->type->isReference)
            errorAtSpan(param->span, QStringLiteral("only any... variadic parameters are supported"));
    }
    if (debt) {
        const Token semi = consume(TokenKind::Semicolon, QStringLiteral("expected ';' after debt function"));
        fn->span = mergeSpans(startSpan, semi.span);
    } else {
        fn->body = parseBlock();
        fn->span = mergeSpans(startSpan, fn->body->span);
    }
    return fn;
}

QString Parser::parseOperatorSymbol()
{
    const Token token = peek();
    switch (token.kind) {
    case TokenKind::Plus:
    case TokenKind::Minus:
    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::Percent:
    case TokenKind::ModMod:
    case TokenKind::Power:
    case TokenKind::MinOp:
    case TokenKind::MaxOp:
    case TokenKind::EqualEqual:
    case TokenKind::BangEqual:
    case TokenKind::Less:
    case TokenKind::LessEqual:
    case TokenKind::Greater:
    case TokenKind::GreaterEqual:
        ++m_pos;
        return token.text;
    default:
        errorAt(token, QStringLiteral("expected overloadable operator symbol"));
        if (!atEnd())
            ++m_pos;
        return {};
    }
}

std::unique_ptr<StructDeclNode> Parser::parseStruct(bool exported)
{
    auto s = std::make_unique<StructDeclNode>();
    const SourceSpan startSpan = previous().span;
    s->exported = exported;
    s->name = consume(TokenKind::Identifier, QStringLiteral("expected struct name")).text;
    consume(TokenKind::LBrace, QStringLiteral("expected '{'"));
    bool membersPrivate = false;
    while (!check(TokenKind::RBrace) && !atEnd()) {
        if (match(TokenKind::KwPublic)) {
            consume(TokenKind::Colon, QStringLiteral("expected ':' after public"));
            membersPrivate = false;
            continue;
        }
        if (match(TokenKind::KwPrivate)) {
            consume(TokenKind::Colon, QStringLiteral("expected ':' after private"));
            membersPrivate = true;
            continue;
        }
        if (match(TokenKind::KwInit)) {
            s->constructors.push_back(parseConstructor(membersPrivate));
        } else {
            const bool constMethod = match(TokenKind::KwConst);
            if (match(TokenKind::KwFn)) {
                auto method = parseFunction(false, false);
                method->isConstMethod = constMethod;
                method->isPrivate = membersPrivate;
                s->methods.push_back(std::move(method));
            } else {
                if (constMethod)
                    errorAt(previous(), QStringLiteral("const only applies to methods in struct body"));
                s->fields.push_back(parseStructField(membersPrivate));
            }
        }
    }
    const Token close = consume(TokenKind::RBrace, QStringLiteral("expected '}'"));
    s->span = mergeSpans(startSpan, close.span);
    return s;
}

std::unique_ptr<FieldDeclNode> Parser::parseStructField(bool isPrivate)
{
    auto field = std::make_unique<FieldDeclNode>();
    field->isPrivate = isPrivate;
    field->type = parseType();
    const Token name = consume(TokenKind::Identifier, QStringLiteral("expected field name"));
    field->name = name.text;
    const Token semi = consume(TokenKind::Semicolon, QStringLiteral("expected ';' after field"));
    field->span = mergeSpans(field->type->span, semi.span);
    return field;
}

std::unique_ptr<EnumDeclNode> Parser::parseEnum(bool exported)
{
    auto e = std::make_unique<EnumDeclNode>();
    const SourceSpan startSpan = previous().span;
    e->exported = exported;
    e->name = consume(TokenKind::Identifier, QStringLiteral("expected enum name")).text;
    consume(TokenKind::LBrace, QStringLiteral("expected '{' after enum name"));
    if (!check(TokenKind::RBrace)) {
        do {
            e->enumerators.push_back(consume(TokenKind::Identifier, QStringLiteral("expected enum enumerator")).text);
        } while (match(TokenKind::Comma) && !check(TokenKind::RBrace));
    }
    const Token close = consume(TokenKind::RBrace, QStringLiteral("expected '}' after enum body"));
    e->span = mergeSpans(startSpan, close.span);
    return e;
}

std::unique_ptr<TypeAliasDeclNode> Parser::parseTypeAlias(bool exported)
{
    auto alias = std::make_unique<TypeAliasDeclNode>();
    const SourceSpan startSpan = previous().span;
    alias->exported = exported;
    alias->name = consume(TokenKind::Identifier, QStringLiteral("expected type alias name")).text;
    consume(TokenKind::Equal, QStringLiteral("expected '=' after type alias name"));
    alias->targetType = parseType();
    const Token semi = consume(TokenKind::Semicolon, QStringLiteral("expected ';' after type alias"));
    alias->span = mergeSpans(startSpan, semi.span);
    return alias;
}

std::unique_ptr<ConstructorDeclNode> Parser::parseConstructor(bool isPrivate)
{
    auto ctor = std::make_unique<ConstructorDeclNode>();
    const SourceSpan startSpan = previous().span;
    ctor->isPrivate = isPrivate;
    consume(TokenKind::LParen, QStringLiteral("expected '(' after init"));
    if (!check(TokenKind::RParen)) {
        do {
            ctor->params.push_back(parseParameter());
        } while (match(TokenKind::Comma));
    }
    consume(TokenKind::RParen, QStringLiteral("expected ')'"));
    ctor->body = parseBlock();
    ctor->span = mergeSpans(startSpan, ctor->body->span);
    return ctor;
}

std::unique_ptr<BackendBlockNode> Parser::parseBackend(bool exported)
{
    auto backend = std::make_unique<BackendBlockNode>();
    const SourceSpan startSpan = previous().span;
    backend->exported = exported;
    backend->name = consume(TokenKind::Identifier, QStringLiteral("expected backend name")).text;
    consume(TokenKind::LBrace, QStringLiteral("expected '{'"));
    while (!check(TokenKind::RBrace) && !atEnd()) {
        consume(TokenKind::KwFn, QStringLiteral("expected backend function declaration"));
        backend->functions.push_back(parseFunction(false, true));
    }
    const Token close = consume(TokenKind::RBrace, QStringLiteral("expected '}'"));
    backend->span = mergeSpans(startSpan, close.span);
    return backend;
}

std::unique_ptr<ParameterNode> Parser::parseParameter()
{
    auto p = std::make_unique<ParameterNode>();
    p->type = parseType();
    if (match(TokenKind::Ellipsis))
        p->variadic = true;
    const Token name = consume(TokenKind::Identifier, QStringLiteral("expected parameter name"));
    p->name = name.text;
    p->span = mergeSpans(p->type->span, name.span);
    if (match(TokenKind::Equal)) {
        p->defaultValue = parseExpression();
        p->span = mergeSpans(p->span, p->defaultValue->span);
    }
    return p;
}

std::unique_ptr<TypeNode> Parser::parseType()
{
    auto t = std::make_unique<TypeNode>();
    SourceSpan startSpan = peek().span;
    t->isConst = match(TokenKind::KwConst);
    if (match(TokenKind::KwVector)) {
        consume(TokenKind::Less, QStringLiteral("expected '<' after vector"));
        t->name = QStringLiteral("vector");
        t->elementType = parseType();
        consume(TokenKind::Greater, QStringLiteral("expected '>' after vector element type"));
    } else if (match(TokenKind::KwFunc)) {
        t->name = QStringLiteral("func");
        t->elementType = parseType();
        consume(TokenKind::LParen, QStringLiteral("expected '(' after func return type"));
        if (!check(TokenKind::RParen)) {
            do {
                t->functionParamTypes.push_back(parseType());
            } while (match(TokenKind::Comma));
        }
        consume(TokenKind::RParen, QStringLiteral("expected ')' after func parameter types"));
    } else if (match(TokenKind::KwAny)) {
        t->name = QStringLiteral("any");
    } else {
        t->name = parseTypeName();
    }
    while (match(TokenKind::Star))
        ++t->pointerDepth;
    if (match(TokenKind::Amp))
        t->isReference = true;
    t->span = mergeSpans(startSpan, previous().span);
    return t;
}

QString Parser::parseTypeName()
{
    QString out = consume(TokenKind::Identifier, QStringLiteral("expected type name")).text;
    for (;;) {
        if (match(TokenKind::Dot)) {
            out += QStringLiteral(".") + consume(TokenKind::Identifier, QStringLiteral("expected identifier after '.' in type name")).text;
        } else if (match(TokenKind::Scope)) {
            out += QStringLiteral("::") + consume(TokenKind::Identifier, QStringLiteral("expected identifier after '::' in type name")).text;
        } else {
            break;
        }
    }
    return out;
}

std::unique_ptr<BlockStmtNode> Parser::parseBlock()
{
    const Token open = consume(TokenKind::LBrace, QStringLiteral("expected '{'"));
    auto block = std::make_unique<BlockStmtNode>();
    while (!check(TokenKind::RBrace) && !isTopLevelDeclarationStart() && !atEnd()) {
        const qsizetype before = m_pos;
        auto stmt = parseStatement();
        if (stmt) {
            block->statements.push_back(std::move(stmt));
            continue;
        }
        if (m_pos == before && !atEnd())
            ++m_pos;
        synchronizeStatement();
    }
    const Token close = consume(TokenKind::RBrace, QStringLiteral("expected '}'"));
    block->span = mergeSpans(open.span, close.span);
    return block;
}

std::unique_ptr<StmtNode> Parser::parseStatement()
{
    if (match(TokenKind::KwReturn)) return parseReturn();
    if (match(TokenKind::KwIf)) return parseIf();
    if (match(TokenKind::KwWhile)) return parseWhile();
    if (match(TokenKind::KwFor)) return parseFor();
    if (match(TokenKind::KwRepeat)) return parseRepeat();
    if (match(TokenKind::KwBreak)) {
        const SourceSpan startSpan = previous().span;
        auto s = std::make_unique<BreakStmtNode>();
        const Token semi = consume(TokenKind::Semicolon, QStringLiteral("expected ';'"));
        s->span = mergeSpans(startSpan, semi.span);
        return s;
    }
    if (match(TokenKind::KwContinue)) {
        const SourceSpan startSpan = previous().span;
        auto s = std::make_unique<ContinueStmtNode>();
        const Token semi = consume(TokenKind::Semicolon, QStringLiteral("expected ';'"));
        s->span = mergeSpans(startSpan, semi.span);
        return s;
    }
    if (check(TokenKind::LBrace)) return parseBlock();
    return parseVarOrExprStatement();
}

std::unique_ptr<StmtNode> Parser::parseIf()
{
    auto ifs = std::make_unique<IfStmtNode>();
    const SourceSpan startSpan = previous().span;
    consume(TokenKind::LParen, QStringLiteral("expected '('"));
    IfStmtNode::Branch first;
    first.condition = parseExpression();
    consume(TokenKind::RParen, QStringLiteral("expected ')'"));
    first.body = parseBlock();
    ifs->branches.push_back(std::move(first));
    while (match(TokenKind::KwElseif)) {
        consume(TokenKind::LParen, QStringLiteral("expected '('"));
        IfStmtNode::Branch branch;
        branch.condition = parseExpression();
        consume(TokenKind::RParen, QStringLiteral("expected ')'"));
        branch.body = parseBlock();
        ifs->branches.push_back(std::move(branch));
    }
    if (match(TokenKind::KwElse)) {
        IfStmtNode::Branch branch;
        branch.body = parseBlock();
        ifs->branches.push_back(std::move(branch));
    }
    if (!ifs->branches.empty())
        ifs->span = mergeSpans(startSpan, ifs->branches.back().body->span);
    return ifs;
}

std::unique_ptr<StmtNode> Parser::parseWhile()
{
    auto s = std::make_unique<WhileStmtNode>();
    const SourceSpan startSpan = previous().span;
    consume(TokenKind::LParen, QStringLiteral("expected '('"));
    s->condition = parseExpression();
    consume(TokenKind::RParen, QStringLiteral("expected ')'"));
    s->body = parseBlock();
    s->span = mergeSpans(startSpan, s->body->span);
    return s;
}

std::unique_ptr<StmtNode> Parser::parseFor()
{
    const SourceSpan startSpan = previous().span;
    consume(TokenKind::LParen, QStringLiteral("expected '('"));
    if (check(TokenKind::Identifier) && peek(1).kind == TokenKind::KwIn) {
        auto s = std::make_unique<RangeForStmtNode>();
        s->variable = consume(TokenKind::Identifier, QStringLiteral("expected range-for variable")).text;
        consume(TokenKind::KwIn, QStringLiteral("expected in"));
        s->range = parseExpression();
        consume(TokenKind::RParen, QStringLiteral("expected ')'"));
        s->body = parseBlock();
        s->span = mergeSpans(startSpan, s->body->span);
        return s;
    }

    auto s = std::make_unique<ForStmtNode>();
    if (!check(TokenKind::Semicolon))
        s->init = parseForInit();
    consume(TokenKind::Semicolon, QStringLiteral("expected ';' after for init"));
    if (!check(TokenKind::Semicolon))
        s->condition = parseExpression();
    consume(TokenKind::Semicolon, QStringLiteral("expected ';' after for condition"));
    if (!check(TokenKind::RParen))
        s->step = parseExpression();
    consume(TokenKind::RParen, QStringLiteral("expected ')'"));
    s->body = parseBlock();
    s->span = mergeSpans(startSpan, s->body->span);
    return s;
}

std::unique_ptr<StmtNode> Parser::parseRepeat()
{
    auto s = std::make_unique<RepeatStmtNode>();
    const SourceSpan startSpan = previous().span;
    consume(TokenKind::LParen, QStringLiteral("expected '('"));
    s->count = parseExpression();
    consume(TokenKind::RParen, QStringLiteral("expected ')'"));
    s->body = parseBlock();
    s->span = mergeSpans(startSpan, s->body->span);
    return s;
}

std::unique_ptr<StmtNode> Parser::parseReturn()
{
    auto s = std::make_unique<ReturnStmtNode>();
    const SourceSpan startSpan = previous().span;
    if (!check(TokenKind::Semicolon))
        s->expr = parseExpression();
    const Token semi = consume(TokenKind::Semicolon, QStringLiteral("expected ';'"));
    s->span = mergeSpans(startSpan, semi.span);
    return s;
}

std::unique_ptr<StmtNode> Parser::parseForInit()
{
    if (looksLikeType()) {
        auto s = std::make_unique<VarDeclStmtNode>();
        s->type = parseType();
        const Token name = consume(TokenKind::Identifier, QStringLiteral("expected variable name"));
        s->name = name.text;
        if (match(TokenKind::Equal))
            s->init = parseExpression();
        s->span = mergeSpans(s->type->span, s->init ? s->init->span : name.span);
        return s;
    }
    auto s = std::make_unique<ExprStmtNode>();
    s->expr = parseExpression();
    s->span = s->expr->span;
    return s;
}

bool Parser::looksLikeType() const
{
    if (check(TokenKind::KwConst) || check(TokenKind::KwVector) || check(TokenKind::KwAny) || check(TokenKind::KwFunc))
        return true;
    if (looksLikeQualifiedTypeName())
        return true;
    if (check(TokenKind::Identifier) && peek(1).kind == TokenKind::Less) {
        int depth = 0;
        for (qsizetype pos = m_pos + 1; pos < m_tokens.size(); ++pos) {
            const TokenKind kind = m_tokens[pos].kind;
            if (kind == TokenKind::Less) {
                ++depth;
            } else if (kind == TokenKind::Greater) {
                --depth;
                if (depth == 0) {
                    ++pos;
                    while (pos < m_tokens.size() && m_tokens[pos].kind == TokenKind::Star)
                        ++pos;
                    if (pos < m_tokens.size() && m_tokens[pos].kind == TokenKind::Amp)
                        ++pos;
                    return pos < m_tokens.size() && m_tokens[pos].kind == TokenKind::Identifier;
                }
            } else if (kind == TokenKind::End || kind == TokenKind::Semicolon || kind == TokenKind::LBrace || kind == TokenKind::RBrace) {
                return false;
            }
        }
        return false;
    }
    if (check(TokenKind::Identifier) && peek(1).kind == TokenKind::Identifier)
        return true;
    if (check(TokenKind::Identifier) && (peek(1).kind == TokenKind::Star || peek(1).kind == TokenKind::Amp))
        return true;
    return false;
}

bool Parser::looksLikeQualifiedTypeName() const
{
    if (!check(TokenKind::Identifier))
        return false;

    qsizetype pos = m_pos + 1;
    bool qualified = false;
    while (pos + 1 < m_tokens.size()) {
        const TokenKind sep = m_tokens[pos].kind;
        if (sep != TokenKind::Dot && sep != TokenKind::Scope)
            break;
        if (m_tokens[pos + 1].kind != TokenKind::Identifier)
            return false;
        qualified = true;
        pos += 2;
    }
    if (!qualified)
        return false;
    while (pos < m_tokens.size() && m_tokens[pos].kind == TokenKind::Star)
        ++pos;
    if (pos < m_tokens.size() && m_tokens[pos].kind == TokenKind::Amp)
        ++pos;
    return pos < m_tokens.size() && m_tokens[pos].kind == TokenKind::Identifier;
}

std::unique_ptr<StmtNode> Parser::parseVarOrExprStatement()
{
    if (looksLikeType()) {
        auto s = std::make_unique<VarDeclStmtNode>();
        s->type = parseType();
        const Token name = consume(TokenKind::Identifier, QStringLiteral("expected variable name"));
        s->name = name.text;
        if (match(TokenKind::Equal))
            s->init = parseExpression();
        const Token semi = consume(TokenKind::Semicolon, QStringLiteral("expected ';'"));
        s->span = mergeSpans(s->type->span, semi.span);
        return s;
    }
    auto s = std::make_unique<ExprStmtNode>();
    s->expr = parseExpression();
    const Token semi = consume(TokenKind::Semicolon, QStringLiteral("expected ';'"));
    s->span = mergeSpans(s->expr->span, semi.span);
    return s;
}

std::unique_ptr<ExprNode> Parser::parseExpression() { return parseAssignment(); }

std::unique_ptr<ExprNode> Parser::parseAssignment()
{
    auto lhs = parseBinary();
    if (match(TokenKind::Equal)) {
        auto node = std::make_unique<AssignExprNode>();
        node->lhs = std::move(lhs);
        node->rhs = parseAssignment();
        node->span = mergeSpans(node->lhs->span, node->rhs->span);
        return node;
    }
    return lhs;
}

int Parser::precedence(TokenKind kind) const
{
    switch (kind) {
    case TokenKind::PipeForward: return 1;
    case TokenKind::OrOr: return 2;
    case TokenKind::AndAnd: return 3;
    case TokenKind::EqualEqual:
    case TokenKind::BangEqual: return 4;
    case TokenKind::Less:
    case TokenKind::LessEqual:
    case TokenKind::Greater:
    case TokenKind::GreaterEqual: return 5;
    case TokenKind::MinOp:
    case TokenKind::MaxOp: return 6;
    case TokenKind::Plus:
    case TokenKind::Minus: return 7;
    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::Percent:
    case TokenKind::ModMod: return 8;
    case TokenKind::Power: return 9;
    default: return 0;
    }
}

QString Parser::opText(const Token& token) const { return token.text; }

std::unique_ptr<ExprNode> Parser::parseBinary(int minPrec)
{
    auto lhs = parseUnary();
    while (precedence(peek().kind) >= minPrec) {
        const Token op = m_tokens[m_pos++];
        const int prec = precedence(op.kind);
        const bool rightAssoc = op.kind == TokenKind::Power;
        auto rhs = parseBinary(prec + (rightAssoc ? 0 : 1));
        auto node = std::make_unique<BinaryExprNode>();
        node->op = opText(op);
        node->lhs = std::move(lhs);
        node->rhs = std::move(rhs);
        node->span = mergeSpans(node->lhs->span, node->rhs->span);
        lhs = std::move(node);
    }
    return lhs;
}

std::unique_ptr<ExprNode> Parser::parseUnary()
{
    if (match(TokenKind::KwCast)) {
        auto node = std::make_unique<CastExprNode>();
        const SourceSpan startSpan = previous().span;
        consume(TokenKind::Less, QStringLiteral("expected '<' after cast"));
        node->targetType = parseType();
        consume(TokenKind::Greater, QStringLiteral("expected '>' after cast target type"));
        consume(TokenKind::LParen, QStringLiteral("expected '(' after cast target type"));
        node->expr = parseExpression();
        const Token close = consume(TokenKind::RParen, QStringLiteral("expected ')' after cast argument"));
        node->span = mergeSpans(startSpan, close.span);
        return node;
    }
    if (match(TokenKind::Bang) || match(TokenKind::Minus) || match(TokenKind::Plus)
        || match(TokenKind::Amp) || match(TokenKind::Star)) {
        auto node = std::make_unique<UnaryExprNode>();
        const SourceSpan startSpan = previous().span;
        node->op = previous().text;
        node->expr = parseUnary();
        node->span = mergeSpans(startSpan, node->expr->span);
        return node;
    }
    return parsePostfix();
}

std::unique_ptr<ExprNode> Parser::parsePostfix()
{
    auto expr = parsePrimary();
    for (;;) {
        if (match(TokenKind::LParen)) {
            auto call = std::make_unique<CallExprNode>();
            const SourceSpan startSpan = expr->span;
            call->callee = std::move(expr);
            parseCallArguments(*call);
            const Token close = consume(TokenKind::RParen, QStringLiteral("expected ')'"));
            call->span = mergeSpans(startSpan, close.span);
            expr = std::move(call);
        } else if (match(TokenKind::LBracket)) {
            auto idx = std::make_unique<IndexExprNode>();
            const SourceSpan startSpan = expr->span;
            idx->base = std::move(expr);
            idx->index = parseExpression();
            const Token close = consume(TokenKind::RBracket, QStringLiteral("expected ']'"));
            idx->span = mergeSpans(startSpan, close.span);
            expr = std::move(idx);
        } else if (match(TokenKind::Dot) || match(TokenKind::Arrow)) {
            auto field = std::make_unique<FieldAccessExprNode>();
            const SourceSpan startSpan = expr->span;
            field->pointer = previous().kind == TokenKind::Arrow;
            field->base = std::move(expr);
            const Token name = consume(TokenKind::Identifier, QStringLiteral("expected field or method name"));
            field->field = name.text;
            field->span = mergeSpans(startSpan, name.span);
            expr = std::move(field);
        } else if (match(TokenKind::Scope)) {
            auto access = std::make_unique<StaticAccessExprNode>();
            const SourceSpan startSpan = expr->span;
            access->base = std::move(expr);
            const Token name = consume(TokenKind::Identifier, QStringLiteral("expected static member name"));
            access->member = name.text;
            access->span = mergeSpans(startSpan, name.span);
            expr = std::move(access);
        } else {
            break;
        }
    }
    return expr;
}

void Parser::parseCallArguments(CallExprNode& call)
{
    if (check(TokenKind::RParen))
        return;
    do {
        QString name;
        bool spread = false;
        if (match(TokenKind::Ellipsis)) {
            spread = true;
        } else if (check(TokenKind::Identifier) && peek(1).kind == TokenKind::Colon) {
            name = consume(TokenKind::Identifier, QStringLiteral("expected argument name")).text;
            consume(TokenKind::Colon, QStringLiteral("expected ':' after argument name"));
        }
        call.args.push_back(parseExpression());
        call.argNames.push_back(std::move(name));
        call.argSpreads.push_back(spread);
    } while (match(TokenKind::Comma));
}

std::unique_ptr<ExprNode> Parser::parsePrimary()
{
    if (check(TokenKind::LBracket))
        return parseBracketLiteral();
    if (match(TokenKind::Integer) || match(TokenKind::Float) || match(TokenKind::String) || match(TokenKind::Char)) {
        auto lit = std::make_unique<LiteralExprNode>();
        const Token& tok = previous();
        lit->text = tok.text;
        lit->kind = tok.kind == TokenKind::Integer ? LiteralExprNode::Kind::Int
            : tok.kind == TokenKind::Float ? LiteralExprNode::Kind::Float
            : tok.kind == TokenKind::String ? LiteralExprNode::Kind::String
            : LiteralExprNode::Kind::Char;
        lit->span = tok.span;
        return lit;
    }
    if (match(TokenKind::KwTrue) || match(TokenKind::KwFalse)) {
        auto lit = std::make_unique<LiteralExprNode>();
        lit->kind = LiteralExprNode::Kind::Bool;
        lit->text = previous().text;
        lit->span = previous().span;
        return lit;
    }
    if (match(TokenKind::KwNullptr)) {
        auto lit = std::make_unique<LiteralExprNode>();
        lit->kind = LiteralExprNode::Kind::Nullptr;
        lit->span = previous().span;
        return lit;
    }
    if (match(TokenKind::KwThis)) {
        auto self = std::make_unique<ThisExprNode>();
        self->span = previous().span;
        return self;
    }
    if (check(TokenKind::KwLambda))
        return parseLambda();
    if (match(TokenKind::Identifier)) {
        auto name = std::make_unique<NameExprNode>();
        name->name = previous().text;
        name->span = previous().span;
        return name;
    }
    if (match(TokenKind::LBrace)) {
        auto init = std::make_unique<InitListExprNode>();
        const SourceSpan startSpan = previous().span;
        if (!check(TokenKind::RBrace)) {
            do {
                init->values.push_back(parseExpression());
            } while (match(TokenKind::Comma));
        }
        const Token close = consume(TokenKind::RBrace, QStringLiteral("expected '}'"));
        init->span = mergeSpans(startSpan, close.span);
        return init;
    }
    if (match(TokenKind::LParen)) {
        auto expr = parseExpression();
        const Token close = consume(TokenKind::RParen, QStringLiteral("expected ')'"));
        expr->span = mergeSpans(expr->span, close.span);
        return expr;
    }
    errorAt(peek(), QStringLiteral("expected expression"));
    auto dummy = std::make_unique<LiteralExprNode>();
    dummy->kind = LiteralExprNode::Kind::Int;
    dummy->text = QStringLiteral("0");
    dummy->span = peek().span;
    return dummy;
}

std::unique_ptr<ExprNode> Parser::parseBracketLiteral()
{
    const Token open = consume(TokenKind::LBracket, QStringLiteral("expected '['"));
    if (match(TokenKind::LBracket)) {
        auto tuple = std::make_unique<AnyTupleLiteralExprNode>();
        const SourceSpan startSpan = open.span;
        if (!check(TokenKind::RBracket)) {
            do {
                tuple->values.push_back(parseExpression());
            } while (match(TokenKind::Comma));
        }
        consume(TokenKind::RBracket, QStringLiteral("expected ']' after anytuple literal"));
        const Token close = consume(TokenKind::RBracket, QStringLiteral("expected ']' after anytuple literal"));
        tuple->span = mergeSpans(startSpan, close.span);
        return tuple;
    }
    if (match(TokenKind::LBrace)) {
        auto map = std::make_unique<StrMapLiteralExprNode>();
        const SourceSpan startSpan = open.span;
        if (!check(TokenKind::RBrace)) {
            do {
                StrMapLiteralExprNode::Entry entry;
                const Token key = consume(TokenKind::String, QStringLiteral("expected string literal key in strmap literal"));
                entry.key = key.text;
                entry.keySpan = key.span;
                consume(TokenKind::Equal, QStringLiteral("expected '=' after strmap key"));
                entry.value = parseExpression();
                map->entries.push_back(std::move(entry));
            } while (match(TokenKind::Comma));
        }
        consume(TokenKind::RBrace, QStringLiteral("expected '}' after strmap literal"));
        const Token close = consume(TokenKind::RBracket, QStringLiteral("expected ']' after strmap literal"));
        map->span = mergeSpans(startSpan, close.span);
        return map;
    }

    errorAt(open, QStringLiteral("expected '[...]' anytuple or '[{...}]' strmap literal"));
    auto dummy = std::make_unique<LiteralExprNode>();
    dummy->kind = LiteralExprNode::Kind::Int;
    dummy->text = QStringLiteral("0");
    dummy->span = open.span;
    return dummy;
}

std::unique_ptr<ExprNode> Parser::parseLambda()
{
    const Token start = consume(TokenKind::KwLambda, QStringLiteral("expected lambda"));
    auto lambda = std::make_unique<LambdaExprNode>();
    consume(TokenKind::LBracket, QStringLiteral("expected '[' after lambda"));
    QStringList captures;
    if (!check(TokenKind::RBracket)) {
        do {
            QString capture;
            if (match(TokenKind::Equal)) {
                capture = QStringLiteral("=");
            } else if (match(TokenKind::Amp)) {
                if (check(TokenKind::Identifier))
                    capture = QStringLiteral("&") + consume(TokenKind::Identifier, QStringLiteral("expected capture name")).text;
                else
                    capture = QStringLiteral("&");
            } else {
                capture = consume(TokenKind::Identifier, QStringLiteral("expected capture")).text;
            }
            captures.push_back(capture);
        } while (match(TokenKind::Comma));
    }
    consume(TokenKind::RBracket, QStringLiteral("expected ']' after lambda capture list"));
    lambda->captureText = captures.join(QStringLiteral(","));
    lambda->returnType = parseType();
    consume(TokenKind::LParen, QStringLiteral("expected '(' after lambda return type"));
    if (!check(TokenKind::RParen)) {
        do {
            auto paramType = parseType();
            QString paramName = consume(TokenKind::Identifier, QStringLiteral("expected lambda parameter name")).text;
            lambda->paramTypes.push_back(std::move(paramType));
            lambda->paramNames.push_back(paramName);
        } while (match(TokenKind::Comma));
    }
    consume(TokenKind::RParen, QStringLiteral("expected ')' after lambda parameters"));
    lambda->ownedBody = parseBlock();
    lambda->span = mergeSpans(start.span, lambda->ownedBody->span);
    return lambda;
}

} // namespace abel
