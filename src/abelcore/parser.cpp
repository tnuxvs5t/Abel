#include "abelcore/parser.h"

#include <QSet>

namespace abel {

ParseResult Parser::parse(const QList<Token>& tokens)
{
    m_tokens = tokens;
    m_pos = 0;
    m_diagnostics.clear();
    auto program = std::make_unique<ProgramNode>();
    while (!atEnd()) {
        auto decl = parseDeclaration();
        if (decl)
            program->declarations.push_back(std::move(decl));
        else if (!atEnd())
            ++m_pos;
    }
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
    if (!atEnd())
        return m_tokens[m_pos++];
    return peek();
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

std::unique_ptr<DeclNode> Parser::parseDeclaration()
{
    bool exported = match(TokenKind::KwExport);
    if (match(TokenKind::KwDebt)) {
        consume(TokenKind::KwFn, QStringLiteral("expected fn after debt"));
        return parseFunction(exported, true);
    }
    if (match(TokenKind::KwBackend))
        return parseBackend(exported);
    if (match(TokenKind::KwFn))
        return parseFunction(exported, false);
    errorAt(peek(), QStringLiteral("expected top-level declaration"));
    return nullptr;
}

std::unique_ptr<FunctionDeclNode> Parser::parseFunction(bool exported, bool debt)
{
    auto fn = std::make_unique<FunctionDeclNode>();
    fn->exported = exported;
    fn->debt = debt;
    fn->returnType = parseType();
    fn->name = consume(TokenKind::Identifier, QStringLiteral("expected function name")).text;
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
        consume(TokenKind::Semicolon, QStringLiteral("expected ';' after debt function"));
    } else {
        fn->body = parseBlock();
    }
    return fn;
}

std::unique_ptr<BackendBlockNode> Parser::parseBackend(bool exported)
{
    auto backend = std::make_unique<BackendBlockNode>();
    backend->exported = exported;
    backend->name = consume(TokenKind::Identifier, QStringLiteral("expected backend name")).text;
    consume(TokenKind::LBrace, QStringLiteral("expected '{'"));
    while (!check(TokenKind::RBrace) && !atEnd()) {
        consume(TokenKind::KwFn, QStringLiteral("expected backend function declaration"));
        backend->functions.push_back(parseFunction(false, true));
    }
    consume(TokenKind::RBrace, QStringLiteral("expected '}'"));
    return backend;
}

std::unique_ptr<ParameterNode> Parser::parseParameter()
{
    auto p = std::make_unique<ParameterNode>();
    p->type = parseType();
    if (match(TokenKind::Ellipsis))
        p->variadic = true;
    p->name = consume(TokenKind::Identifier, QStringLiteral("expected parameter name")).text;
    return p;
}

std::unique_ptr<TypeNode> Parser::parseType()
{
    auto t = std::make_unique<TypeNode>();
    t->isConst = match(TokenKind::KwConst);
    if (match(TokenKind::KwVector)) {
        consume(TokenKind::Less, QStringLiteral("expected '<' after vector"));
        t->name = QStringLiteral("vector");
        t->elementType = parseType();
        consume(TokenKind::Greater, QStringLiteral("expected '>' after vector element type"));
    } else if (match(TokenKind::KwAny)) {
        t->name = QStringLiteral("any");
    } else {
        t->name = consume(TokenKind::Identifier, QStringLiteral("expected type name")).text;
    }
    while (match(TokenKind::Star))
        ++t->pointerDepth;
    if (match(TokenKind::Amp))
        t->isReference = true;
    return t;
}

std::unique_ptr<BlockStmtNode> Parser::parseBlock()
{
    consume(TokenKind::LBrace, QStringLiteral("expected '{'"));
    auto block = std::make_unique<BlockStmtNode>();
    while (!check(TokenKind::RBrace) && !atEnd())
        block->statements.push_back(parseStatement());
    consume(TokenKind::RBrace, QStringLiteral("expected '}'"));
    return block;
}

std::unique_ptr<StmtNode> Parser::parseStatement()
{
    if (match(TokenKind::KwReturn)) return parseReturn();
    if (match(TokenKind::KwIf)) return parseIf();
    if (match(TokenKind::KwWhile)) return parseWhile();
    if (match(TokenKind::KwRepeat)) return parseRepeat();
    if (match(TokenKind::KwBreak)) {
        auto s = std::make_unique<BreakStmtNode>();
        consume(TokenKind::Semicolon, QStringLiteral("expected ';'"));
        return s;
    }
    if (match(TokenKind::KwContinue)) {
        auto s = std::make_unique<ContinueStmtNode>();
        consume(TokenKind::Semicolon, QStringLiteral("expected ';'"));
        return s;
    }
    if (check(TokenKind::LBrace)) return parseBlock();
    return parseVarOrExprStatement();
}

std::unique_ptr<StmtNode> Parser::parseIf()
{
    auto ifs = std::make_unique<IfStmtNode>();
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
    return ifs;
}

std::unique_ptr<StmtNode> Parser::parseWhile()
{
    auto s = std::make_unique<WhileStmtNode>();
    consume(TokenKind::LParen, QStringLiteral("expected '('"));
    s->condition = parseExpression();
    consume(TokenKind::RParen, QStringLiteral("expected ')'"));
    s->body = parseBlock();
    return s;
}

std::unique_ptr<StmtNode> Parser::parseRepeat()
{
    auto s = std::make_unique<RepeatStmtNode>();
    consume(TokenKind::LParen, QStringLiteral("expected '('"));
    s->count = parseExpression();
    consume(TokenKind::RParen, QStringLiteral("expected ')'"));
    s->body = parseBlock();
    return s;
}

std::unique_ptr<StmtNode> Parser::parseReturn()
{
    auto s = std::make_unique<ReturnStmtNode>();
    if (!check(TokenKind::Semicolon))
        s->expr = parseExpression();
    consume(TokenKind::Semicolon, QStringLiteral("expected ';'"));
    return s;
}

bool Parser::looksLikeType() const
{
    if (check(TokenKind::KwConst) || check(TokenKind::KwVector) || check(TokenKind::KwAny))
        return true;
    if (check(TokenKind::Identifier) && peek(1).kind == TokenKind::Identifier)
        return true;
    if (check(TokenKind::Identifier) && (peek(1).kind == TokenKind::Star || peek(1).kind == TokenKind::Amp))
        return true;
    return false;
}

std::unique_ptr<StmtNode> Parser::parseVarOrExprStatement()
{
    if (looksLikeType()) {
        auto s = std::make_unique<VarDeclStmtNode>();
        s->type = parseType();
        s->name = consume(TokenKind::Identifier, QStringLiteral("expected variable name")).text;
        if (match(TokenKind::Equal))
            s->init = parseExpression();
        consume(TokenKind::Semicolon, QStringLiteral("expected ';'"));
        return s;
    }
    auto s = std::make_unique<ExprStmtNode>();
    s->expr = parseExpression();
    consume(TokenKind::Semicolon, QStringLiteral("expected ';'"));
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
        lhs = std::move(node);
    }
    return lhs;
}

std::unique_ptr<ExprNode> Parser::parseUnary()
{
    if (match(TokenKind::Bang) || match(TokenKind::Minus) || match(TokenKind::Plus)
        || match(TokenKind::Amp) || match(TokenKind::Star)) {
        auto node = std::make_unique<UnaryExprNode>();
        node->op = previous().text;
        node->expr = parseUnary();
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
            call->callee = std::move(expr);
            if (!check(TokenKind::RParen)) {
                do {
                    call->args.push_back(parseExpression());
                } while (match(TokenKind::Comma));
            }
            consume(TokenKind::RParen, QStringLiteral("expected ')'"));
            expr = std::move(call);
        } else if (match(TokenKind::LBracket)) {
            auto idx = std::make_unique<IndexExprNode>();
            idx->base = std::move(expr);
            idx->index = parseExpression();
            consume(TokenKind::RBracket, QStringLiteral("expected ']'"));
            expr = std::move(idx);
        } else if (match(TokenKind::Dot) || match(TokenKind::Arrow)) {
            auto field = std::make_unique<FieldAccessExprNode>();
            field->pointer = previous().kind == TokenKind::Arrow;
            field->base = std::move(expr);
            field->field = consume(TokenKind::Identifier, QStringLiteral("expected field or method name")).text;
            expr = std::move(field);
        } else if (match(TokenKind::Scope)) {
            auto access = std::make_unique<StaticAccessExprNode>();
            access->base = std::move(expr);
            access->member = consume(TokenKind::Identifier, QStringLiteral("expected static member name")).text;
            expr = std::move(access);
        } else {
            break;
        }
    }
    return expr;
}

std::unique_ptr<ExprNode> Parser::parsePrimary()
{
    if (match(TokenKind::Integer) || match(TokenKind::Float) || match(TokenKind::String) || match(TokenKind::Char)) {
        auto lit = std::make_unique<LiteralExprNode>();
        const Token& tok = previous();
        lit->text = tok.text;
        lit->kind = tok.kind == TokenKind::Integer ? LiteralExprNode::Kind::Int
            : tok.kind == TokenKind::Float ? LiteralExprNode::Kind::Float
            : tok.kind == TokenKind::String ? LiteralExprNode::Kind::String
            : LiteralExprNode::Kind::Char;
        return lit;
    }
    if (match(TokenKind::KwTrue) || match(TokenKind::KwFalse)) {
        auto lit = std::make_unique<LiteralExprNode>();
        lit->kind = LiteralExprNode::Kind::Bool;
        lit->text = previous().text;
        return lit;
    }
    if (match(TokenKind::KwNullptr)) {
        auto lit = std::make_unique<LiteralExprNode>();
        lit->kind = LiteralExprNode::Kind::Nullptr;
        return lit;
    }
    if (match(TokenKind::Identifier)) {
        auto name = std::make_unique<NameExprNode>();
        name->name = previous().text;
        return name;
    }
    if (match(TokenKind::LBrace)) {
        auto init = std::make_unique<InitListExprNode>();
        if (!check(TokenKind::RBrace)) {
            do {
                init->values.push_back(parseExpression());
            } while (match(TokenKind::Comma));
        }
        consume(TokenKind::RBrace, QStringLiteral("expected '}'"));
        return init;
    }
    if (match(TokenKind::LParen)) {
        auto expr = parseExpression();
        consume(TokenKind::RParen, QStringLiteral("expected ')'"));
        return expr;
    }
    errorAt(peek(), QStringLiteral("expected expression"));
    auto dummy = std::make_unique<LiteralExprNode>();
    dummy->kind = LiteralExprNode::Kind::Int;
    dummy->text = QStringLiteral("0");
    return dummy;
}

} // namespace abel
