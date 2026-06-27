#pragma once

#include "abelcore/ast.h"
#include "abelcore/builtin_registry.h"
#include "abelcore/diagnostic.h"

#include <QHash>
#include <QList>
#include <QSet>

#include <optional>

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

    struct FieldInfo {
        AbelType type;
        bool isConst = false;
        bool isPrivate = false;
    };

    struct StructInfo {
        const StructDeclNode* decl = nullptr;
        QHash<QString, FieldInfo> fields;
        QHash<QString, QList<const FunctionDeclNode*>> methods;
        QList<const ConstructorDeclNode*> constructors;
    };

    struct BackendInfo {
        const BackendBlockNode* decl = nullptr;
        QHash<QString, const FunctionDeclNode*> functions;
    };

    struct EnumInfo {
        const EnumDeclNode* decl = nullptr;
        QHash<QString, int> values;
    };

    struct NormalizedCallArgs {
        bool ok = true;
        std::vector<ExprType> checked;
        std::vector<SourceSpan> spans;
        std::vector<bool> defaulted;
        std::vector<bool> pipeHoles;
    };

    QHash<QString, QList<const FunctionDeclNode*>> m_functions;
    QList<const FunctionDeclNode*> m_functionDecls;
    QHash<QString, QList<StructInfo>> m_structs;
    QHash<QString, QList<BackendInfo>> m_backends;
    QHash<QString, QList<EnumInfo>> m_enums;
    QHash<QString, QList<const TypeAliasDeclNode*>> m_typeAliases;
    QSet<QString> m_resolvingTypeAliases;
    QList<QHash<QString, VariableInfo>> m_scopes;
    QList<Diagnostic> m_diagnostics;
    BuiltinRegistry m_builtins = BuiltinRegistry::makeDefault();
    AbelType m_currentReturnType = makeType(TypeKind::Void);
    std::optional<ExprType> m_pipeHoleExpr;
    QString m_currentStruct;
    QString m_currentPackage;
    QString m_currentModule;
    QList<QString> m_currentImports;
    QHash<QString, QString> m_currentImportAliases;
    int m_loopDepth = 0;

    struct BuiltPipeArgs {
        std::vector<ExprType> checked;
        std::vector<SourceSpan> spans;
        std::vector<bool> holes;
        std::vector<QString> names;
        std::vector<bool> spreads;
    };

    void collectStructs(const ProgramNode& program);
    void collectEnums(const ProgramNode& program);
    void collectTypeAliases(const ProgramNode& program);
    void collectFunctions(const ProgramNode& program);
    bool sameFunctionSignature(const FunctionDeclNode& lhs, const FunctionDeclNode& rhs);
    bool sameConstructorSignature(const StructDeclNode& owner, const ConstructorDeclNode& lhs, const ConstructorDeclNode& rhs);
    void collectBackends(const ProgramNode& program);
    const FunctionDeclNode* findRootFunction(const QString& name) const;
    QList<const FunctionDeclNode*> resolveFunctionCandidates(const QString& name, const SourceSpan& span, bool diagnose = true);
    const FunctionDeclNode* resolveFunction(const QString& name, const SourceSpan& span, bool diagnose = true);
    QList<const FunctionDeclNode*> resolveFunctionCandidatesInModule(const QString& moduleName,
                                                                     const QString& name,
                                                                     const SourceSpan& span,
                                                                     bool diagnose = true);
    const FunctionDeclNode* resolveFunctionInModule(const QString& moduleName,
                                                    const QString& name,
                                                    const SourceSpan& span,
                                                    bool diagnose = true);
    const StructInfo* resolveStruct(const QString& name, const SourceSpan& span, bool diagnose = true);
    const StructInfo* resolveStructInModule(const QString& moduleName,
                                            const QString& name,
                                            const SourceSpan& span,
                                            bool diagnose = true);
    const StructInfo* resolveStructInPackage(const QString& name,
                                             const QString& packageName,
                                             const SourceSpan& span,
                                             bool diagnose = true);
    const StructInfo* structInfoForType(const AbelType& type) const;
    const EnumInfo* resolveEnum(const QString& name, const SourceSpan& span, bool diagnose = true);
    const EnumInfo* resolveEnumInModule(const QString& moduleName,
                                        const QString& name,
                                        const SourceSpan& span,
                                        bool diagnose = true);
    const EnumInfo* resolveEnumInPackage(const QString& name,
                                         const QString& packageName,
                                         const SourceSpan& span,
                                         bool diagnose = true);
    const TypeAliasDeclNode* resolveTypeAlias(const QString& name, const QString& packageName, const SourceSpan& span, bool diagnose = true);
    const TypeAliasDeclNode* resolveTypeAliasInModule(const QString& moduleName,
                                                      const QString& name,
                                                      const SourceSpan& span,
                                                      bool diagnose = true);
    const BackendInfo* resolveBackend(const QString& name, const SourceSpan& span, bool diagnose = true);
    const BackendInfo* resolveBackendInModule(const QString& moduleName,
                                              const QString& name,
                                              const SourceSpan& span,
                                              bool diagnose = true);
    const BackendInfo* resolveBackendInPackage(const QString& name,
                                               const QString& packageName,
                                               const SourceSpan& span,
                                               bool diagnose = true);
    AbelType typeFromAstInCurrentPackage(const TypeNode& node);
    AbelType typeFromAstInPackage(const TypeNode& node, const QString& packageName, bool diagnose = true);
    AbelType typeFromAstForDecl(const TypeNode& node, const DeclNode& decl, bool diagnose = true);
    FieldInfo fieldInfoForStructField(const StructInfo& info, const AbelType& structType, const FieldDeclNode& field);
    void checkFunction(const FunctionDeclNode& fn);
    void checkTypeAlias(const TypeAliasDeclNode& alias);
    void checkStruct(const StructDeclNode& decl);
    void checkBackend(const BackendBlockNode& backend);
    void checkConstructor(const StructDeclNode& owner, const ConstructorDeclNode& ctor, const AbelType* selfType = nullptr);
    void checkMethod(const StructDeclNode& owner, const FunctionDeclNode& method, const AbelType* selfType = nullptr);
    void checkBlock(const BlockStmtNode& block, bool pushScope);
    void checkStmt(const StmtNode& stmt);
    void checkVarDecl(const VarDeclStmtNode& stmt);
    void checkFor(const ForStmtNode& stmt);
    void checkRangeFor(const RangeForStmtNode& stmt);

    ExprType checkExpr(const ExprNode& expr);
    ExprType checkName(const NameExprNode& expr);
    ExprType checkUnary(const UnaryExprNode& expr);
    ExprType checkBinary(const BinaryExprNode& expr);
    ExprType checkUserBinaryOperator(const QString& op,
                                     const ExprType& lhs,
                                     const ExprType& rhs,
                                     const SourceSpan& span,
                                     const QString& fallbackMessage);
    ExprType checkCast(const CastExprNode& expr);
    ExprType checkPipe(const BinaryExprNode& expr);
    ExprType checkPipeTarget(const QString& name,
                             const SourceSpan& nameSpan,
                             const ExprType& lhs,
                             const std::vector<std::unique_ptr<ExprNode>>& args,
                             const SourceSpan& span,
                             const CallExprNode* sourceCall = nullptr);
    BuiltPipeArgs buildPipeArgs(const ExprType& lhs,
                                const std::vector<std::unique_ptr<ExprNode>>& args,
                                const SourceSpan& span,
                                const CallExprNode* sourceCall);
    CallExprNode makeSyntheticPipeCall(const ExprNode* callee,
                                       const CallExprNode* source,
                                       const BuiltPipeArgs& built,
                                       const SourceSpan& span);
    ExprType checkFunctionCallShape(const QString& name,
                                    const FunctionDeclNode& fn,
                                    const ExprType& firstArg,
                                    const std::vector<std::unique_ptr<ExprNode>>& restArgs,
                                    const SourceSpan& span);
    ExprType checkFunctionOverloadCall(const QString& displayName,
                                       const QList<const FunctionDeclNode*>& candidates,
                                       const std::vector<ExprType>& args,
                                       const std::vector<SourceSpan>& argSpans,
                                       const SourceSpan& span,
                                       const std::vector<std::unique_ptr<TypeNode>>* explicitTypeArgs = nullptr,
                                       bool hasExplicitTypeArgs = false,
                                       const std::vector<bool>* pipeHoleArgs = nullptr);
    ExprType checkStructuredFunctionOverloadCall(const QString& displayName,
                                                 const QList<const FunctionDeclNode*>& candidates,
                                                 const CallExprNode& call);
    ExprType checkStructuredFunctionOverloadCallWithArgs(const QString& displayName,
                                                         const QList<const FunctionDeclNode*>& candidates,
                                                         const CallExprNode& call,
                                                         const std::vector<ExprType>& rawArgs,
                                                         const std::vector<bool>* rawPipeHoles = nullptr);
    ExprType checkMethodOverloadCall(const QString& displayName,
                                     const QList<const FunctionDeclNode*>& candidates,
                                     const ExprType& receiver,
                                     const CallExprNode& call,
                                     const SourceSpan& span,
                                     bool receiverPipeHole = false,
                                     const std::vector<bool>* rawPipeHoles = nullptr,
                                     int totalPipeHoles = 0);
    ExprType checkFunctionValueCallShape(const AbelType& functionType,
                                         const ExprType& firstArg,
                                         const std::vector<std::unique_ptr<ExprNode>>& restArgs,
                                         const SourceSpan& span);
    ExprType checkAssignment(const AssignExprNode& expr);
    ExprType checkCall(const CallExprNode& expr);
    ExprType checkStaticCall(const StaticAccessExprNode& callee,
                             const CallExprNode& call,
                             const std::vector<ExprType>* precheckedArgs = nullptr,
                             const std::vector<bool>* rawPipeHoles = nullptr);
    ExprType checkBackendCall(const StaticAccessExprNode& callee,
                              const CallExprNode& call,
                              const std::vector<ExprType>* precheckedArgs = nullptr,
                              const std::vector<bool>* rawPipeHoles = nullptr);
    ExprType checkBackendCallByName(const QString& backendName,
                                    const SourceSpan& backendSpan,
                                    const QString& member,
                                    const CallExprNode& call,
                                    const std::vector<ExprType>* precheckedArgs = nullptr,
                                    const std::vector<bool>* rawPipeHoles = nullptr);
    ExprType checkQualifiedFunctionCall(const QString& moduleName,
                                        const SourceSpan& moduleSpan,
                                        const QString& name,
                                        const CallExprNode& call,
                                        const std::vector<ExprType>* precheckedArgs = nullptr,
                                        const std::vector<bool>* rawPipeHoles = nullptr);
    ExprType checkStructConstructorCall(const QString& displayName,
                                        const StructInfo& info,
                                        const CallExprNode& call,
                                        const AbelType* constructedType = nullptr,
                                        const std::vector<ExprType>* precheckedArgs = nullptr,
                                        const std::vector<bool>* rawPipeHoles = nullptr);
    ExprType checkFunctionValueCall(const AbelType& functionType, const std::vector<std::unique_ptr<ExprNode>>& args, const SourceSpan& span);
    ExprType checkLambda(const LambdaExprNode& expr);
    ExprType checkFieldAccess(const FieldAccessExprNode& expr);
    ExprType checkBuiltinMethodCall(const FieldAccessExprNode& callee,
                                    const std::vector<std::unique_ptr<ExprNode>>& args,
                                    bool receiverPipeHole = false,
                                    int totalPipeHoles = 0);
    ExprType checkIndex(const IndexExprNode& expr);
    ExprType checkInitListAgainst(const InitListExprNode& init, const AbelType& target);
    void checkParameterArgument(const AbelType& paramType,
                                const ExprType& arg,
                                const SourceSpan& argSpan,
                                const QString& label);
    std::optional<int> scoreParameterArgument(const AbelType& paramType, const ExprType& arg) const;
    NormalizedCallArgs normalizeCallArgsForParams(const DeclNode& decl,
                                                  const QString& displayName,
                                                  const std::vector<std::unique_ptr<ParameterNode>>& params,
                                                  const CallExprNode& call,
                                                  const std::vector<ExprType>& rawArgs,
                                                  bool diagnose,
                                                  const std::vector<bool>* rawPipeHoles = nullptr);
    void checkParameterDefault(const DeclNode& decl,
                               const std::vector<std::unique_ptr<ParameterNode>>& params,
                               size_t index,
                               const AbelType& paramType);

    void pushScope();
    void popScope();
    void defineVariable(const QString& name, const AbelType& type, bool isConst, const SourceSpan& span);
    const VariableInfo* lookupVariable(const QString& name) const;

    bool isSupportedType(const AbelType& type);
    bool isDeclInCurrentModule(const DeclNode& decl, const QString& packageName = QString()) const;
    bool isDeclVisible(const DeclNode& decl) const;
    bool isModuleImported(const QString& moduleName) const;
    QString resolveModuleName(const QString& moduleName) const;
    bool isDeclVisibleInCurrentContext(const DeclNode& decl, bool exportedSymbol) const;
    bool isFunctionVisible(const FunctionDeclNode& fn) const;
    bool isStructVisible(const StructDeclNode& decl) const;
    bool isBackendVisible(const BackendBlockNode& backend) const;
    bool isEnumVisible(const EnumDeclNode& decl) const;
    bool isTypeAliasVisible(const TypeAliasDeclNode& alias) const;
    bool requireFunctionVisible(const FunctionDeclNode& fn, const SourceSpan& span);
    bool requireStructVisible(const StructDeclNode& decl, const SourceSpan& span);
    bool requireBackendVisible(const BackendBlockNode& backend, const SourceSpan& span);
    bool isDefaultConstructible(const AbelType& type);
    bool isDefaultConstructible(const AbelType& type, QSet<QString>& visiting);
    bool isDefaultConstructionAccessible(const AbelType& type);
    bool isAssignable(const AbelType& target, const AbelType& source) const;
    bool isStringifiable(const AbelType& type);
    bool hasUserToStrFor(const AbelType& type);
    AbelType valueTypeOfVariable(const AbelType& type) const;
    ExprType errorExpr(const SourceSpan& span, const QString& message);
    void error(const SourceSpan& span, const QString& message, const QString& code = QStringLiteral("E0301"));
    void errorDetailed(const SourceSpan& span,
                       const QString& message,
                       const QList<SourceSpan>& related,
                       const QString& explanation = {},
                       const QStringList& suggestions = {},
                       const QString& code = QStringLiteral("E0301"));
};

} // namespace abel
