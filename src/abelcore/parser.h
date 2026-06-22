#pragma once

#include "abelcore/ast.h"
#include "abelcore/diagnostic.h"
#include "abelcore/token.h"

#include <QList>

namespace abel {

struct ParseResult {
    std::unique_ptr<ProgramNode> program;
    QList<Diagnostic> diagnostics;
};

class Parser {
public:
    ParseResult parse(const QList<Token>& tokens);

private:
    QList<Token> m_tokens;
    qsizetype m_pos = 0;
    QList<Diagnostic> m_diagnostics;

    const Token& peek(int offset = 0) const;
    const Token& previous() const;
    bool atEnd() const;
    bool check(TokenKind kind) const;
    bool match(TokenKind kind);
    Token consume(TokenKind kind, const QString& message);
    void errorAt(const Token& token, const QString& message);
    void errorAtSpan(const SourceSpan& span, const QString& message);

    std::unique_ptr<DeclNode> parseDeclaration();
    std::unique_ptr<FunctionDeclNode> parseFunction(bool exported, bool debt);
    std::unique_ptr<StructDeclNode> parseStruct(bool exported);
    std::unique_ptr<FieldDeclNode> parseStructField();
    std::unique_ptr<ConstructorDeclNode> parseConstructor();
    std::unique_ptr<BackendBlockNode> parseBackend(bool exported);
    std::unique_ptr<ParameterNode> parseParameter();
    std::unique_ptr<TypeNode> parseType();
    std::unique_ptr<BlockStmtNode> parseBlock();
    std::unique_ptr<StmtNode> parseStatement();
    std::unique_ptr<StmtNode> parseIf();
    std::unique_ptr<StmtNode> parseWhile();
    std::unique_ptr<StmtNode> parseFor();
    std::unique_ptr<StmtNode> parseRepeat();
    std::unique_ptr<StmtNode> parseReturn();
    std::unique_ptr<StmtNode> parseForInit();
    std::unique_ptr<StmtNode> parseVarOrExprStatement();

    std::unique_ptr<ExprNode> parseExpression();
    std::unique_ptr<ExprNode> parseAssignment();
    std::unique_ptr<ExprNode> parseBinary(int minPrec = 1);
    std::unique_ptr<ExprNode> parseUnary();
    std::unique_ptr<ExprNode> parsePostfix();
    std::unique_ptr<ExprNode> parsePrimary();

    bool looksLikeType() const;
    int precedence(TokenKind kind) const;
    QString opText(const Token& token) const;
};

} // namespace abel
