#pragma once

#include "abelcore/ast.h"
#include "abelcore/builtin_registry.h"
#include "abelcore/diagnostic.h"

#include <QHash>
#include <QList>

namespace abel {

struct TypeCheckResult {
    QList<Diagnostic> diagnostics;
};

enum class ValueCategory {
    LValue,
    PRValue,
};

struct ExprType {
    AbelType type;
    ValueCategory category = ValueCategory::PRValue;
    bool isMutable = true;
};

class TypeChecker {
public:
    TypeCheckResult check(const ProgramNode& program);

private:
    struct VariableInfo {
        AbelType type;
        bool isConst = false;
    };

    QHash<QString, const FunctionDeclNode*> m_functions;
    QList<QHash<QString, VariableInfo>> m_scopes;
    QList<Diagnostic> m_diagnostics;
    BuiltinRegistry m_builtins = BuiltinRegistry::makeDefault();
    AbelType m_currentReturnType = makeType(TypeKind::Void);
    int m_loopDepth = 0;

    void collectFunctions(const ProgramNode& program);
    void checkFunction(const FunctionDeclNode& fn);
    void checkBlock(const BlockStmtNode& block, bool pushScope);
    void checkStmt(const StmtNode& stmt);
    void checkVarDecl(const VarDeclStmtNode& stmt);
    void checkFor(const ForStmtNode& stmt);
    void checkRangeFor(const RangeForStmtNode& stmt);

    ExprType checkExpr(const ExprNode& expr);
    ExprType checkName(const NameExprNode& expr);
    ExprType checkUnary(const UnaryExprNode& expr);
    ExprType checkBinary(const BinaryExprNode& expr);
    ExprType checkAssignment(const AssignExprNode& expr);
    ExprType checkCall(const CallExprNode& expr);
    ExprType checkBuiltinMethodCall(const FieldAccessExprNode& callee, const std::vector<std::unique_ptr<ExprNode>>& args);
    ExprType checkIndex(const IndexExprNode& expr);
    ExprType checkInitListAgainst(const InitListExprNode& init, const AbelType& target);

    void pushScope();
    void popScope();
    void defineVariable(const QString& name, const AbelType& type, bool isConst, const SourceSpan& span);
    const VariableInfo* lookupVariable(const QString& name) const;

    bool isSupportedType(const AbelType& type) const;
    bool isAssignable(const AbelType& target, const AbelType& source) const;
    AbelType valueTypeOfVariable(const AbelType& type) const;
    ExprType errorExpr(const SourceSpan& span, const QString& message);
    void error(const SourceSpan& span, const QString& message, const QString& code = QStringLiteral("E0301"));
};

} // namespace abel
