#pragma once

#include "abelcore/ast.h"
#include "abelcore/backend_registry.h"
#include "abelcore/builtin_registry.h"
#include "abelcore/runtime.h"

#include <QHash>
#include <QSet>

#include <optional>

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

    QHash<QString, QList<const FunctionDeclNode*>> m_functions;
    QString m_currentPackage;
    QString m_currentModule;
    QList<QString> m_currentImports;
    QHash<QString, QString> m_currentImportAliases;
    QHash<QString, QList<StructRuntimeInfo>> m_structs;
    QHash<QString, QList<BackendRuntimeInfo>> m_backends;
    BackendRegistry m_backendRegistry;
    BackendRegistry* m_activeBackendRegistry = nullptr;
    BuiltinRegistry m_builtins = BuiltinRegistry::makeDefault();
    AbelRuntimeContext* m_ctx = nullptr;

    bool collectFunctions(const ProgramNode& program, AbelRuntimeContext& ctx);
    bool collectStructs(const ProgramNode& program, AbelRuntimeContext& ctx);
    bool collectBackends(const ProgramNode& program, AbelRuntimeContext& ctx);
    const FunctionDeclNode* findRootFunction(const QString& name) const;
    const FunctionDeclNode* resolveFunction(const QString& name) const;
    const FunctionDeclNode* resolveFunctionInModule(const QString& moduleName, const QString& name) const;
    const StructRuntimeInfo* resolveStruct(const QString& name) const;
    const StructRuntimeInfo* resolveStructInModule(const QString& moduleName, const QString& name) const;
    const StructRuntimeInfo* resolveStructInPackage(const QString& name, const QString& packageName) const;
    const StructRuntimeInfo* structInfoForType(const AbelType& type) const;
    const BackendRuntimeInfo* resolveBackend(const QString& name) const;
    const BackendRuntimeInfo* resolveBackendInModule(const QString& moduleName, const QString& name) const;
    const BackendRuntimeInfo* resolveBackendInPackage(const QString& name, const QString& packageName) const;
    AbelType typeFromAstInCurrentPackage(const TypeNode& node) const;
    AbelType typeFromAstInPackage(const TypeNode& node, const QString& packageName) const;
    AbelType typeFromAstForDecl(const TypeNode& node, const DeclNode& decl);
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
    AbelValue evalStaticCall(const StaticAccessExprNode& callee, const std::vector<std::unique_ptr<ExprNode>>& args, const SourceSpan& span);
    AbelValue evalBackendCall(const StaticAccessExprNode& callee, const std::vector<std::unique_ptr<ExprNode>>& args, const SourceSpan& span);
    AbelValue evalBackendCallByName(const QString& backendName,
                                    const SourceSpan& backendSpan,
                                    const QString& member,
                                    const std::vector<std::unique_ptr<ExprNode>>& args,
                                    const SourceSpan& span);
    AbelValue evalQualifiedFunctionCall(const QString& moduleName,
                                        const QString& name,
                                        const std::vector<std::unique_ptr<ExprNode>>& args,
                                        const SourceSpan& span);
    AbelValue evalQualifiedStructConstructor(const QString& moduleName,
                                             const QString& name,
                                             const std::vector<std::unique_ptr<ExprNode>>& args,
                                             const SourceSpan& span);
    AbelValue evalLambda(const LambdaExprNode& expr);
    AbelValue evalBuiltinMethod(const FieldAccessExprNode& callee, const std::vector<std::unique_ptr<ExprNode>>& args);
    AbelValue evalStructConstructor(const QString& name, const StructRuntimeInfo& info, const std::vector<std::unique_ptr<ExprNode>>& args, const SourceSpan& span);
    AbelValue evalStructMethod(const FieldAccessExprNode& callee, const std::vector<std::unique_ptr<ExprNode>>& args, const SourceSpan& span);
    AbelValue evalAssignment(const AssignExprNode& expr);
    AbelValue defaultConstructValue(const AbelType& type, const SourceSpan& span);
    AbelValue defaultConstructValue(const AbelType& type, const SourceSpan& span, QSet<QString>& visiting);
    std::optional<QString> stringifyValue(const AbelValue& value, const SourceSpan& span);
    void attachStringifier(BuiltinFunctionCall& call);

    bool isDeclInCurrentModule(const DeclNode& decl, const QString& packageName = QString()) const;
    bool isModuleImported(const QString& moduleName) const;
    QString resolveModuleName(const QString& moduleName) const;
    bool isDeclVisible(const DeclNode& decl, bool exportedSymbol) const;
    bool structFieldReadOnly(const AbelType& structType, const QString& fieldName);

    bool requireBool(const AbelValue& value, const SourceSpan& span, bool& out);
    bool requireInteger(const AbelValue& value, const SourceSpan& span, qint64& out);
    AbelValue convertOrError(const AbelValue& value, const AbelType& target, const SourceSpan& span);
    bool isReadOnlyBinding(const AbelType& type, bool syntacticConst) const;
    bool canBindReferenceValue(const AbelType& referenceType, const AbelType& sourceType) const;
    void error(const QString& code, const QString& message, const SourceSpan& span);
};

} // namespace abel
