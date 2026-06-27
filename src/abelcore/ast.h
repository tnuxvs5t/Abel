#pragma once

#include "abelcore/source_span.h"

#include <QHash>
#include <QString>
#include <QList>

#include <memory>
#include <vector>

namespace abel {

struct AstNode {
    SourceSpan span;
    virtual ~AstNode() = default;
};

struct TypeNode : AstNode {
    QString name;
    bool isConst = false;
    int pointerDepth = 0;
    bool isReference = false;
    std::unique_ptr<TypeNode> elementType; // vector<T> element or func return type
    std::vector<std::unique_ptr<TypeNode>> functionParamTypes;
    std::vector<std::unique_ptr<TypeNode>> typeArguments; // parser recovery for retired named generic syntax

    QString displayName() const;
};

struct ExprNode : AstNode {
    virtual ~ExprNode() = default;
};

struct LiteralExprNode final : ExprNode {
    enum class Kind { Int, Float, String, Char, Bool, Nullptr };
    Kind kind = Kind::Int;
    QString text;
};

struct NameExprNode final : ExprNode {
    QString name;
};

struct UnaryExprNode final : ExprNode {
    QString op;
    std::unique_ptr<ExprNode> expr;
};

struct BinaryExprNode final : ExprNode {
    QString op;
    std::unique_ptr<ExprNode> lhs;
    std::unique_ptr<ExprNode> rhs;
};

struct AssignExprNode final : ExprNode {
    std::unique_ptr<ExprNode> lhs;
    std::unique_ptr<ExprNode> rhs;
};

struct CastExprNode final : ExprNode {
    std::unique_ptr<TypeNode> targetType;
    std::unique_ptr<ExprNode> expr;
};

struct CallExprNode final : ExprNode {
    std::unique_ptr<ExprNode> callee;
    bool hasExplicitTypeArgs = false;
    std::vector<std::unique_ptr<TypeNode>> explicitTypeArgs;
    std::vector<std::unique_ptr<ExprNode>> args;
    std::vector<QString> argNames; // empty for positional
    std::vector<bool> argSpreads;
};

struct IndexExprNode final : ExprNode {
    std::unique_ptr<ExprNode> base;
    std::unique_ptr<ExprNode> index;
};

struct FieldAccessExprNode final : ExprNode {
    std::unique_ptr<ExprNode> base;
    QString field;
    bool pointer = false;
};

struct StaticAccessExprNode final : ExprNode {
    std::unique_ptr<ExprNode> base;
    QString member;
};

struct ThisExprNode final : ExprNode {};

struct InitListExprNode final : ExprNode {
    std::vector<std::unique_ptr<ExprNode>> values;
};

struct LambdaExprNode final : ExprNode {
    QString captureText;
    std::unique_ptr<TypeNode> returnType;
    QList<QString> paramNames;
    std::vector<std::unique_ptr<TypeNode>> paramTypes;
    std::unique_ptr<struct BlockStmtNode> ownedBody;
};

struct StmtNode : AstNode {
    virtual ~StmtNode() = default;
};

struct BlockStmtNode final : StmtNode {
    std::vector<std::unique_ptr<StmtNode>> statements;
};

struct ExprStmtNode final : StmtNode {
    std::unique_ptr<ExprNode> expr;
};

struct ReturnStmtNode final : StmtNode {
    std::unique_ptr<ExprNode> expr;
};

struct VarDeclStmtNode final : StmtNode {
    bool isConst = false;
    std::unique_ptr<TypeNode> type;
    QString name;
    std::unique_ptr<ExprNode> init;
};

struct IfStmtNode final : StmtNode {
    struct Branch {
        std::unique_ptr<ExprNode> condition; // null for else
        std::unique_ptr<BlockStmtNode> body;
    };
    std::vector<Branch> branches;
};

struct WhileStmtNode final : StmtNode {
    std::unique_ptr<ExprNode> condition;
    std::unique_ptr<BlockStmtNode> body;
};

struct RepeatStmtNode final : StmtNode {
    std::unique_ptr<ExprNode> count;
    std::unique_ptr<BlockStmtNode> body;
};

struct ForStmtNode final : StmtNode {
    std::unique_ptr<StmtNode> init; // VarDeclStmtNode or ExprStmtNode, optional
    std::unique_ptr<ExprNode> condition;
    std::unique_ptr<ExprNode> step;
    std::unique_ptr<BlockStmtNode> body;
};

struct RangeForStmtNode final : StmtNode {
    QString variable;
    std::unique_ptr<ExprNode> range;
    std::unique_ptr<BlockStmtNode> body;
};

struct BreakStmtNode final : StmtNode {};
struct ContinueStmtNode final : StmtNode {};

struct ParameterNode : AstNode {
    std::unique_ptr<TypeNode> type;
    QString name;
    bool variadic = false;
    std::unique_ptr<ExprNode> defaultValue;
};

struct DeclNode : AstNode {
    QString packageName;
    QString moduleName;
    QList<QString> importedModules;
    QHash<QString, QString> importedModuleAliases;
    bool fromDependency = false;

    virtual ~DeclNode() = default;
};

struct ModuleDeclNode final : DeclNode {
    QString name;
};

struct UseDeclNode final : DeclNode {
    QString name;
    QString alias;
    bool exported = false;
};

struct FunctionDeclNode final : DeclNode {
    bool exported = false;
    bool debt = false;
    bool isConstMethod = false;
    bool isPrivate = false;
    bool isOperator = false;
    QString operatorSymbol;
    std::unique_ptr<TypeNode> returnType;
    QString name;
    std::vector<std::unique_ptr<ParameterNode>> params;
    std::unique_ptr<BlockStmtNode> body;
};

struct FieldDeclNode : AstNode {
    std::unique_ptr<TypeNode> type;
    QString name;
    bool isPrivate = false;
};

struct ConstructorDeclNode : AstNode {
    std::vector<std::unique_ptr<ParameterNode>> params;
    std::unique_ptr<BlockStmtNode> body;
    bool isPrivate = false;
};

struct StructDeclNode final : DeclNode {
    bool exported = false;
    QString name;
    std::vector<std::unique_ptr<FieldDeclNode>> fields;
    std::vector<std::unique_ptr<ConstructorDeclNode>> constructors;
    std::vector<std::unique_ptr<FunctionDeclNode>> methods;
};

struct EnumDeclNode final : DeclNode {
    bool exported = false;
    QString name;
    QList<QString> enumerators;
};

struct TypeAliasDeclNode final : DeclNode {
    bool exported = false;
    QString name;
    std::unique_ptr<TypeNode> targetType;
};

struct BackendBlockNode final : DeclNode {
    bool exported = false;
    QString name;
    std::vector<std::unique_ptr<FunctionDeclNode>> functions;
};

struct ProgramNode final : AstNode {
    std::vector<std::unique_ptr<DeclNode>> declarations;
};

} // namespace abel
