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
    Token syntheticToken(TokenKind kind, const SourceSpan& span) const;
    void errorAt(const Token& token, const QString& message);
    void errorAtSpan(const SourceSpan& span, const QString& message);
    bool shouldNotConsumeMissing(TokenKind expected, TokenKind found) const;
    bool isTopLevelDeclarationStart(TokenKind kind) const;
    bool isTopLevelDeclarationStart() const;
    bool isStatementStart(TokenKind kind) const;
    void synchronizeDeclaration();
    void synchronizeStatement();

    std::unique_ptr<DeclNode> parseDeclaration();
    std::unique_ptr<ModuleDeclNode> parseModule();
    std::unique_ptr<UseDeclNode> parseUse(bool exported = false);
    QString parseQualifiedName(const QString& what);
    std::unique_ptr<FunctionDeclNode> parseFunction(bool exported, bool debt);
    QString parseOperatorSymbol();
    std::unique_ptr<StructDeclNode> parseStruct(bool exported);
    std::unique_ptr<EnumDeclNode> parseEnum(bool exported);
    std::unique_ptr<TypeAliasDeclNode> parseTypeAlias(bool exported);
    std::unique_ptr<FieldDeclNode> parseStructField(bool isPrivate);
    std::unique_ptr<ConstructorDeclNode> parseConstructor(bool isPrivate);
    std::unique_ptr<BackendBlockNode> parseBackend(bool exported);
    std::unique_ptr<ParameterNode> parseParameter();
    std::unique_ptr<TypeNode> parseType();
    QString parseTypeName();
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
    std::unique_ptr<ExprNode> parseLambda();

    bool looksLikeType() const;
    bool looksLikeQualifiedTypeName() const;
    int precedence(TokenKind kind) const;
    QString opText(const Token& token) const;
};

} // namespace abel
