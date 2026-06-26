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
    InterpreterResult runTest(const ProgramNode& program);
    InterpreterResult runTest(const ProgramNode& program, BackendRegistry* backendRegistry);

private:
    struct StructRuntimeInfo {
        const StructDeclNode* decl = nullptr;
        QList<const ConstructorDeclNode*> constructors;
        QHash<QString, QList<const FunctionDeclNode*>> methods;
    };

    struct BackendRuntimeInfo {
        const BackendBlockNode* decl = nullptr;
        QHash<QString, const FunctionDeclNode*> functions;
    };

    struct EnumRuntimeInfo {
        const EnumDeclNode* decl = nullptr;
        QHash<QString, int> values;
    };

    struct PreparedCallArg {
        AbelValue value = AbelValue::makeUnknown();
        AbelLocation* location = nullptr;
        bool isReadOnly = false;
        SourceSpan span;
    };

    QHash<QString, QList<const FunctionDeclNode*>> m_functions;
    QString m_currentPackage;
    QString m_currentModule;
    QString m_currentStruct;
    QList<QString> m_currentImports;
    QHash<QString, QString> m_currentImportAliases;
    QHash<QString, QList<StructRuntimeInfo>> m_structs;
    QHash<QString, QList<BackendRuntimeInfo>> m_backends;
    QHash<QString, QList<EnumRuntimeInfo>> m_enums;
    QHash<QString, QList<const TypeAliasDeclNode*>> m_typeAliases;
    QHash<QString, AbelType> m_templateTypes;
    QHash<QString, QHash<QString, AbelType>> m_structTemplateInstantiations;
    QSet<QString> m_resolvingTypeAliases;
    BackendRegistry m_backendRegistry;
    BackendRegistry* m_activeBackendRegistry = nullptr;
    BuiltinRegistry m_builtins = BuiltinRegistry::makeDefault();
    AbelRuntimeContext* m_ctx = nullptr;
    bool m_hasPipeHoleArg = false;
    PreparedCallArg m_pipeHoleArg;
    AbelLocation* m_pipeHoleTempLocation = nullptr;

    void beginRun(AbelRuntimeContext& ctx, BackendRegistry* backendRegistry);
    void endRun();
    bool collectProgram(const ProgramNode& program, AbelRuntimeContext& ctx, InterpreterResult& result);
    bool validateTestFixture(const FunctionDeclNode* fn, const QString& name, AbelRuntimeContext& ctx);
    bool collectFunctions(const ProgramNode& program, AbelRuntimeContext& ctx);
    bool collectStructs(const ProgramNode& program, AbelRuntimeContext& ctx);
    bool collectEnums(const ProgramNode& program, AbelRuntimeContext& ctx);
    bool collectTypeAliases(const ProgramNode& program, AbelRuntimeContext& ctx);
    bool collectBackends(const ProgramNode& program, AbelRuntimeContext& ctx);
    bool sameFunctionSignature(const FunctionDeclNode& lhs, const FunctionDeclNode& rhs);
    bool sameConstructorSignature(const StructDeclNode& owner, const ConstructorDeclNode& lhs, const ConstructorDeclNode& rhs);
    const FunctionDeclNode* findRootFunction(const QString& name) const;
    const FunctionDeclNode* findRootFunctionInFile(const QString& name, const QString& file) const;
    QList<const FunctionDeclNode*> resolveFunctionCandidates(const QString& name) const;
    const FunctionDeclNode* resolveFunction(const QString& name) const;
    QList<const FunctionDeclNode*> resolveFunctionCandidatesInModule(const QString& moduleName, const QString& name) const;
    const FunctionDeclNode* resolveFunctionInModule(const QString& moduleName, const QString& name) const;
    const StructRuntimeInfo* resolveStruct(const QString& name) const;
    const StructRuntimeInfo* resolveStructInModule(const QString& moduleName, const QString& name) const;
    const StructRuntimeInfo* resolveStructInPackage(const QString& name, const QString& packageName) const;
    const StructRuntimeInfo* structInfoForType(const AbelType& type) const;
    const EnumRuntimeInfo* resolveEnum(const QString& name) const;
    const EnumRuntimeInfo* resolveEnumInModule(const QString& moduleName, const QString& name) const;
    const EnumRuntimeInfo* resolveEnumInPackage(const QString& name, const QString& packageName) const;
    const TypeAliasDeclNode* resolveTypeAlias(const QString& name, const QString& packageName) const;
    const TypeAliasDeclNode* resolveTypeAliasInModule(const QString& moduleName, const QString& name) const;
    const BackendRuntimeInfo* resolveBackend(const QString& name) const;
    const BackendRuntimeInfo* resolveBackendInModule(const QString& moduleName, const QString& name) const;
    const BackendRuntimeInfo* resolveBackendInPackage(const QString& name, const QString& packageName) const;
    AbelType typeFromAstInCurrentPackage(const TypeNode& node);
    AbelType typeFromAstInPackage(const TypeNode& node, const QString& packageName);
    AbelType typeFromAstForDecl(const TypeNode& node, const DeclNode& decl);
    std::optional<QHash<QString, AbelType>> bindTypeTemplateParams(const std::vector<QString>& params,
                                                                   const std::vector<std::unique_ptr<TypeNode>>& args,
                                                                   const DeclNode& decl,
                                                                   const SourceSpan& span);
    QString templateTypeInstantiationName(const DeclNode& decl,
                                          const QString& name,
                                          const std::vector<QString>& params,
                                          const QHash<QString, AbelType>& bindings) const;
    std::optional<QHash<QString, AbelType>> structTemplateBindingsForType(const StructDeclNode& decl, const AbelType& type) const;
    ExecResult callFunction(const FunctionDeclNode& fn, const std::vector<AbelValue>& args);
    ExecResult callFunctionExpr(const FunctionDeclNode& fn, const std::vector<std::unique_ptr<ExprNode>>& args, const SourceSpan& span);
    ExecResult callFunctionPipeExpr(const FunctionDeclNode& fn,
                                    const ExprNode& firstArg,
                                    const std::vector<std::unique_ptr<ExprNode>>& restArgs,
                                    const SourceSpan& span);
    std::vector<PreparedCallArg> prepareFunctionArgs(const std::vector<std::unique_ptr<ExprNode>>& args);
    std::vector<PreparedCallArg> prepareFunctionPipeArgs(const ExprNode& firstArg,
                                                         const std::vector<std::unique_ptr<ExprNode>>& restArgs);
    std::optional<int> scorePreparedArgument(const AbelType& paramType, const PreparedCallArg& arg) const;
    const FunctionDeclNode* selectFunctionOverload(const QString& displayName,
                                                   const QList<const FunctionDeclNode*>& candidates,
                                                   const std::vector<PreparedCallArg>& args,
                                                   const SourceSpan& span,
                                                   const std::vector<std::unique_ptr<TypeNode>>* explicitTypeArgs = nullptr,
                                                   bool hasExplicitTypeArgs = false,
                                                   QHash<QString, AbelType>* outTemplateBindings = nullptr);
    ExecResult callFunctionPrepared(const FunctionDeclNode& fn,
                                    const std::vector<PreparedCallArg>& args,
                                    const SourceSpan& span,
                                    const QHash<QString, AbelType>* templateBindings = nullptr);
    ExecResult callFunctionOverloadExpr(const QString& displayName,
                                        const QList<const FunctionDeclNode*>& candidates,
                                        const std::vector<std::unique_ptr<ExprNode>>& args,
                                        const SourceSpan& span,
                                        const std::vector<std::unique_ptr<TypeNode>>* explicitTypeArgs = nullptr,
                                        bool hasExplicitTypeArgs = false);
    ExecResult callFunctionOverloadPipeExpr(const QString& displayName,
                                            const QList<const FunctionDeclNode*>& candidates,
                                            const ExprNode& firstArg,
                                            const std::vector<std::unique_ptr<ExprNode>>& restArgs,
                                            const SourceSpan& span);
    ExecResult callStructFunctionPrepared(const FunctionDeclNode& fn,
                                          AbelLocation* self,
                                          const std::vector<PreparedCallArg>& args,
                                          const SourceSpan& span);
    ExecResult callStructFunctionOverloadExpr(const QString& displayName,
                                              const QList<const FunctionDeclNode*>& candidates,
                                              AbelLocation* self,
                                              const std::vector<std::unique_ptr<ExprNode>>& args,
                                              const SourceSpan& span);
    const ConstructorDeclNode* selectConstructorOverload(const QString& displayName,
                                                        const StructRuntimeInfo& info,
                                                        const std::vector<PreparedCallArg>& args,
                                                        const SourceSpan& span);
    AbelValue callFunctionValue(const AbelValue& fnValue, const std::vector<std::unique_ptr<ExprNode>>& args, const SourceSpan& span);
    AbelValue callFunctionValuePipe(const AbelValue& fnValue,
                                    const ExprNode& firstArg,
                                    const std::vector<std::unique_ptr<ExprNode>>& restArgs,
                                    const SourceSpan& span);
    ExecResult execBlock(const BlockStmtNode& block);
    ExecResult execStmt(const StmtNode& stmt);
    ExecResult execVarDecl(const VarDeclStmtNode& stmt);
    ExecResult execFor(const ForStmtNode& stmt);
    ExecResult execRangeFor(const RangeForStmtNode& stmt);
    AbelValue evalExpr(const ExprNode& expr);
    AbelLocation* evalLocation(const ExprNode& expr);
    AbelValue evalBinary(const BinaryExprNode& expr);
    std::optional<AbelValue> evalUserBinaryOperator(const QString& op,
                                                    const AbelValue& lhs,
                                                    const AbelValue& rhs,
                                                    const SourceSpan& span);
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
    AbelValue makeFunctionValue(const FunctionDeclNode& fn);
    AbelValue evalBuiltinMethod(const FieldAccessExprNode& callee, const std::vector<std::unique_ptr<ExprNode>>& args);
    AbelValue evalStructConstructor(const QString& name,
                                    const StructRuntimeInfo& info,
                                    const std::vector<std::unique_ptr<ExprNode>>& args,
                                    const SourceSpan& span,
                                    const AbelType* constructedType = nullptr);
    AbelValue evalStructConstructorPrepared(const QString& name,
                                            const StructRuntimeInfo& info,
                                            const ConstructorDeclNode* ctor,
                                            const std::vector<PreparedCallArg>& args,
                                            const SourceSpan& span,
                                            const AbelType* constructedType = nullptr);
    AbelValue evalStructMethod(const FieldAccessExprNode& callee, const std::vector<std::unique_ptr<ExprNode>>& args, const SourceSpan& span);
    AbelValue evalAssignment(const AssignExprNode& expr);
    AbelValue defaultConstructValue(const AbelType& type, const SourceSpan& span);
    AbelValue defaultConstructValue(const AbelType& type, const SourceSpan& span, QSet<QString>& visiting);
    std::optional<QString> stringifyValue(const AbelValue& value, const SourceSpan& span);
    void attachStringifier(BuiltinFunctionCall& call);
    std::optional<QString> readScanToken(const SourceSpan& span);

    bool isDeclInCurrentModule(const DeclNode& decl, const QString& packageName = QString()) const;
    bool isModuleImported(const QString& moduleName) const;
    QString resolveModuleName(const QString& moduleName) const;
    bool isDeclVisible(const DeclNode& decl, bool exportedSymbol) const;
    bool isEnumVisible(const EnumDeclNode& decl) const;
    bool isTypeAliasVisible(const TypeAliasDeclNode& alias) const;
    AbelType structFieldType(const AbelType& structType, const QString& fieldName);
    bool structFieldReadOnly(const AbelType& structType, const QString& fieldName);
    bool structFieldPrivate(const AbelType& structType, const QString& fieldName) const;
    bool isCurrentStruct(const StructRuntimeInfo& info) const;

    bool requireBool(const AbelValue& value, const SourceSpan& span, bool& out);
    bool requireInteger(const AbelValue& value, const SourceSpan& span, qint64& out);
    AbelValue convertOrError(const AbelValue& value, const AbelType& target, const SourceSpan& span);
    bool isReadOnlyBinding(const AbelType& type, bool syntacticConst) const;
    bool canBindReferenceValue(const AbelType& referenceType, const AbelType& sourceType) const;
    std::optional<int> scoreValueArgument(const AbelType& paramType, const AbelValue& arg) const;
    bool bindTemplateTypeName(const QString& name, const AbelType& type, QHash<QString, AbelType>& bindings) const;
    bool inferTemplateTypes(const TypeNode& pattern, const PreparedCallArg& arg, QHash<QString, AbelType>& bindings);
    std::optional<QHash<QString, AbelType>> bindFunctionTemplate(const FunctionDeclNode& fn,
                                                                 const std::vector<PreparedCallArg>& args,
                                                                 const std::vector<std::unique_ptr<TypeNode>>* explicitTypeArgs,
                                                                 bool hasExplicitTypeArgs);
    void error(const QString& code, const QString& message, const SourceSpan& span);
};

} // namespace abel
