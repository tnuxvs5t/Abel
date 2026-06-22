#pragma once

#include "abelcore/ast.h"
#include "abelcore/builtin_registry.h"
#include "abelcore/runtime.h"

#include <QHash>

namespace abel {

struct InterpreterResult {
    int exitCode = 0;
    AbelValue returnValue = AbelValue::makeVoid();
    QList<Diagnostic> diagnostics;
};

class Interpreter {
public:
    InterpreterResult run(const ProgramNode& program);

private:
    QHash<QString, const FunctionDeclNode*> m_functions;
    BuiltinRegistry m_builtins = BuiltinRegistry::makeDefault();
    AbelRuntimeContext* m_ctx = nullptr;

    bool collectFunctions(const ProgramNode& program, AbelRuntimeContext& ctx);
    ExecResult callFunction(const FunctionDeclNode& fn, const std::vector<AbelValue>& args);
    ExecResult callFunctionExpr(const FunctionDeclNode& fn, const std::vector<std::unique_ptr<ExprNode>>& args, const SourceSpan& span);
    ExecResult execBlock(const BlockStmtNode& block);
    ExecResult execStmt(const StmtNode& stmt);
    ExecResult execVarDecl(const VarDeclStmtNode& stmt);
    AbelValue evalExpr(const ExprNode& expr);
    AbelLocation* evalLocation(const ExprNode& expr);
    AbelValue evalBinary(const BinaryExprNode& expr);
    AbelValue evalUnary(const UnaryExprNode& expr);
    AbelValue evalCall(const CallExprNode& expr);
    AbelValue evalBuiltinMethod(const FieldAccessExprNode& callee, const std::vector<std::unique_ptr<ExprNode>>& args);
    AbelValue evalAssignment(const AssignExprNode& expr);

    bool requireBool(const AbelValue& value, const SourceSpan& span, bool& out);
    bool requireInteger(const AbelValue& value, const SourceSpan& span, qint64& out);
    AbelValue convertOrError(const AbelValue& value, const AbelType& target, const SourceSpan& span);
    void error(const QString& code, const QString& message, const SourceSpan& span);
};

} // namespace abel
