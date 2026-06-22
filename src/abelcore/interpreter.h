#pragma once

#include "abelcore/ast.h"
#include "abelcore/backend_registry.h"
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
    InterpreterResult run(const ProgramNode& program, BackendRegistry* backendRegistry);

private:
    struct StructRuntimeInfo {
        const StructDeclNode* decl = nullptr;
        const ConstructorDeclNode* constructor = nullptr;
        QHash<QString, const FunctionDeclNode*> methods;
    };

    struct BackendRuntimeInfo {
        const BackendBlockNode* decl = nullptr;
        QHash<QString, const FunctionDeclNode*> functions;
    };

    QHash<QString, const FunctionDeclNode*> m_functions;
    QHash<QString, StructRuntimeInfo> m_structs;
    QHash<QString, BackendRuntimeInfo> m_backends;
    BackendRegistry m_backendRegistry;
    BackendRegistry* m_activeBackendRegistry = nullptr;
    BuiltinRegistry m_builtins = BuiltinRegistry::makeDefault();
    AbelRuntimeContext* m_ctx = nullptr;

    bool collectFunctions(const ProgramNode& program, AbelRuntimeContext& ctx);
    bool collectStructs(const ProgramNode& program, AbelRuntimeContext& ctx);
    bool collectBackends(const ProgramNode& program, AbelRuntimeContext& ctx);
    ExecResult callFunction(const FunctionDeclNode& fn, const std::vector<AbelValue>& args);
    ExecResult callFunctionExpr(const FunctionDeclNode& fn, const std::vector<std::unique_ptr<ExprNode>>& args, const SourceSpan& span);
    ExecResult callFunctionPipeExpr(const FunctionDeclNode& fn,
                                    const ExprNode& firstArg,
                                    const std::vector<std::unique_ptr<ExprNode>>& restArgs,
                                    const SourceSpan& span);
    AbelValue callFunctionValue(const AbelValue& fnValue, const std::vector<std::unique_ptr<ExprNode>>& args, const SourceSpan& span);
    AbelValue callFunctionValuePipe(const AbelValue& fnValue,
                                    const ExprNode& firstArg,
                                    const std::vector<std::unique_ptr<ExprNode>>& restArgs,
                                    const SourceSpan& span);
    ExecResult callStructFunction(const FunctionDeclNode& fn, AbelLocation* self, const std::vector<std::unique_ptr<ExprNode>>& args, const SourceSpan& span);
    ExecResult execBlock(const BlockStmtNode& block);
    ExecResult execStmt(const StmtNode& stmt);
    ExecResult execVarDecl(const VarDeclStmtNode& stmt);
    ExecResult execFor(const ForStmtNode& stmt);
    ExecResult execRangeFor(const RangeForStmtNode& stmt);
    AbelValue evalExpr(const ExprNode& expr);
    AbelLocation* evalLocation(const ExprNode& expr);
    AbelValue evalBinary(const BinaryExprNode& expr);
    AbelValue evalUnary(const UnaryExprNode& expr);
    AbelValue evalCast(const CastExprNode& expr);
    AbelValue evalPipe(const BinaryExprNode& expr);
    AbelValue evalCall(const CallExprNode& expr);
    AbelValue evalBackendCall(const StaticAccessExprNode& callee, const std::vector<std::unique_ptr<ExprNode>>& args, const SourceSpan& span);
    AbelValue evalLambda(const LambdaExprNode& expr);
    AbelValue evalBuiltinMethod(const FieldAccessExprNode& callee, const std::vector<std::unique_ptr<ExprNode>>& args);
    AbelValue evalStructConstructor(const QString& name, const StructRuntimeInfo& info, const std::vector<std::unique_ptr<ExprNode>>& args, const SourceSpan& span);
    AbelValue evalStructMethod(const FieldAccessExprNode& callee, const std::vector<std::unique_ptr<ExprNode>>& args, const SourceSpan& span);
    AbelValue evalAssignment(const AssignExprNode& expr);

    bool requireBool(const AbelValue& value, const SourceSpan& span, bool& out);
    bool requireInteger(const AbelValue& value, const SourceSpan& span, qint64& out);
    AbelValue convertOrError(const AbelValue& value, const AbelType& target, const SourceSpan& span);
    void error(const QString& code, const QString& message, const SourceSpan& span);
};

} // namespace abel
