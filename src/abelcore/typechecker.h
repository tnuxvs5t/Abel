#pragma once

#include "abelcore/ast.h"
#include "abelcore/builtin_registry.h"
#include "abelcore/diagnostic.h"

#include <QHash>
#include <QList>
#include <QSet>

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
    };

    struct StructInfo {
        const StructDeclNode* decl = nullptr;
        QHash<QString, FieldInfo> fields;
        QHash<QString, const FunctionDeclNode*> methods;
        const ConstructorDeclNode* constructor = nullptr;
    };

    struct BackendInfo {
        const BackendBlockNode* decl = nullptr;
        QHash<QString, const FunctionDeclNode*> functions;
    };

    QHash<QString, QList<const FunctionDeclNode*>> m_functions;
    QList<const FunctionDeclNode*> m_functionDecls;
    QHash<QString, QList<StructInfo>> m_structs;
    QHash<QString, QList<BackendInfo>> m_backends;
    QList<QHash<QString, VariableInfo>> m_scopes;
    QList<Diagnostic> m_diagnostics;
    BuiltinRegistry m_builtins = BuiltinRegistry::makeDefault();
    AbelType m_currentReturnType = makeType(TypeKind::Void);
    QString m_currentStruct;
    QString m_currentPackage;
    int m_loopDepth = 0;

    void collectStructs(const ProgramNode& program);
    void collectFunctions(const ProgramNode& program);
    void collectBackends(const ProgramNode& program);
    const FunctionDeclNode* findRootFunction(const QString& name) const;
    const FunctionDeclNode* resolveFunction(const QString& name, const SourceSpan& span, bool diagnose = true);
    const StructInfo* resolveStruct(const QString& name, const SourceSpan& span, bool diagnose = true);
    const StructInfo* resolveStructInPackage(const QString& name,
                                             const QString& packageName,
                                             const SourceSpan& span,
                                             bool diagnose = true);
    const StructInfo* structInfoForType(const AbelType& type) const;
    const BackendInfo* resolveBackend(const QString& name, const SourceSpan& span, bool diagnose = true);
    const BackendInfo* resolveBackendInPackage(const QString& name,
                                               const QString& packageName,
                                               const SourceSpan& span,
                                               bool diagnose = true);
    AbelType typeFromAstInCurrentPackage(const TypeNode& node);
    AbelType typeFromAstInPackage(const TypeNode& node, const QString& packageName, bool diagnose = true);
    void checkFunction(const FunctionDeclNode& fn);
    void checkStruct(const StructDeclNode& decl);
    void checkBackend(const BackendBlockNode& backend);
    void checkConstructor(const StructDeclNode& owner, const ConstructorDeclNode& ctor);
    void checkMethod(const StructDeclNode& owner, const FunctionDeclNode& method);
    void checkBlock(const BlockStmtNode& block, bool pushScope);
    void checkStmt(const StmtNode& stmt);
    void checkVarDecl(const VarDeclStmtNode& stmt);
    void checkFor(const ForStmtNode& stmt);
    void checkRangeFor(const RangeForStmtNode& stmt);

    ExprType checkExpr(const ExprNode& expr);
    ExprType checkName(const NameExprNode& expr);
    ExprType checkUnary(const UnaryExprNode& expr);
    ExprType checkBinary(const BinaryExprNode& expr);
    ExprType checkCast(const CastExprNode& expr);
    ExprType checkPipe(const BinaryExprNode& expr);
    ExprType checkPipeTarget(const QString& name,
                             const SourceSpan& nameSpan,
                             const ExprType& lhs,
                             const std::vector<std::unique_ptr<ExprNode>>& args,
                             const SourceSpan& span);
    ExprType checkFunctionCallShape(const QString& name,
                                    const FunctionDeclNode& fn,
                                    const ExprType& firstArg,
                                    const std::vector<std::unique_ptr<ExprNode>>& restArgs,
                                    const SourceSpan& span);
    ExprType checkFunctionValueCallShape(const AbelType& functionType,
                                         const ExprType& firstArg,
                                         const std::vector<std::unique_ptr<ExprNode>>& restArgs,
                                         const SourceSpan& span);
    ExprType checkAssignment(const AssignExprNode& expr);
    ExprType checkCall(const CallExprNode& expr);
    ExprType checkBackendCall(const StaticAccessExprNode& callee, const std::vector<std::unique_ptr<ExprNode>>& args, const SourceSpan& span);
    ExprType checkFunctionValueCall(const AbelType& functionType, const std::vector<std::unique_ptr<ExprNode>>& args, const SourceSpan& span);
    ExprType checkLambda(const LambdaExprNode& expr);
    ExprType checkFieldAccess(const FieldAccessExprNode& expr);
    ExprType checkBuiltinMethodCall(const FieldAccessExprNode& callee, const std::vector<std::unique_ptr<ExprNode>>& args);
    ExprType checkIndex(const IndexExprNode& expr);
    ExprType checkInitListAgainst(const InitListExprNode& init, const AbelType& target);

    void pushScope();
    void popScope();
    void defineVariable(const QString& name, const AbelType& type, bool isConst, const SourceSpan& span);
    const VariableInfo* lookupVariable(const QString& name) const;

    bool isSupportedType(const AbelType& type);
    bool isDeclVisible(const DeclNode& decl) const;
    bool isFunctionVisible(const FunctionDeclNode& fn) const;
    bool isStructVisible(const StructDeclNode& decl) const;
    bool isBackendVisible(const BackendBlockNode& backend) const;
    bool requireFunctionVisible(const FunctionDeclNode& fn, const SourceSpan& span);
    bool requireStructVisible(const StructDeclNode& decl, const SourceSpan& span);
    bool requireBackendVisible(const BackendBlockNode& backend, const SourceSpan& span);
    bool isDefaultConstructible(const AbelType& type);
    bool isDefaultConstructible(const AbelType& type, QSet<QString>& visiting);
    bool isAssignable(const AbelType& target, const AbelType& source) const;
    bool isStringifiable(const AbelType& type);
    bool hasUserToStrFor(const AbelType& type);
    AbelType valueTypeOfVariable(const AbelType& type) const;
    ExprType errorExpr(const SourceSpan& span, const QString& message);
    void error(const SourceSpan& span, const QString& message, const QString& code = QStringLiteral("E0301"));
};

} // namespace abel
