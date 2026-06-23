#include "abelcore/typechecker.h"

#include <QSet>

#include <optional>

namespace abel {

namespace {

bool isSourceLocationBuiltinName(const QString& name)
{
    return name == QStringLiteral("__FILE__")
        || name == QStringLiteral("__LINE__")
        || name == QStringLiteral("__COLUMN__");
}

AbelType sourceLocationBuiltinType(const QString& name)
{
    if (name == QStringLiteral("__FILE__"))
        return makeType(TypeKind::Str);
    return makeType(TypeKind::I32);
}

bool isUnknownType(const AbelType& type)
{
    return type.kind == TypeKind::Unknown;
}

ExprType unknownExprType()
{
    return {makeType(TypeKind::Unknown), ValueCategory::PRValue, false};
}

} // namespace

TypeCheckResult TypeChecker::check(const ProgramNode& program)
{
    m_functions.clear();
    m_backends.clear();
    m_scopes.clear();
    m_diagnostics.clear();
    m_currentReturnType = makeType(TypeKind::Void);
    m_currentStruct.clear();
    m_loopDepth = 0;

    collectStructs(program);
    collectFunctions(program);
    collectBackends(program);

    const FunctionDeclNode* main = m_functions.value(QStringLiteral("main"), nullptr);
    if (!main) {
        error(program.span, QStringLiteral("missing fn int main() or fn void main()"));
    } else {
        const AbelType mainType = typeFromAst(*main->returnType);
        if (mainType.kind != TypeKind::I32 && mainType.kind != TypeKind::Void)
            error(main->span, QStringLiteral("main must return int or void"));
        if (!main->params.empty())
            error(main->span, QStringLiteral("main must not take parameters in v0"));
    }

    for (const auto& fn : m_functions)
        checkFunction(*fn);
    for (const auto& info : m_structs)
        checkStruct(*info.decl);
    for (const auto& info : m_backends)
        checkBackend(*info.decl);

    return {m_diagnostics};
}

void TypeChecker::collectStructs(const ProgramNode& program)
{
    m_structs.clear();
    for (const auto& decl : program.declarations) {
        auto* s = dynamic_cast<StructDeclNode*>(decl.get());
        if (!s)
            continue;
        if (m_structs.contains(s->name)) {
            error(s->span, QStringLiteral("duplicate struct '%1'").arg(s->name));
            continue;
        }
        StructInfo info;
        info.decl = s;
        for (const auto& field : s->fields) {
            if (info.fields.contains(field->name)) {
                error(field->span, QStringLiteral("duplicate field '%1'").arg(field->name));
                continue;
            }
            AbelType type = typeFromAst(*field->type);
            if (type.isReference())
                error(field->span, QStringLiteral("reference fields are not supported in v0"));
            info.fields.insert(field->name, FieldInfo{type, field->type->isConst});
        }
        for (const auto& method : s->methods) {
            if (info.methods.contains(method->name)) {
                error(method->span, QStringLiteral("duplicate method '%1'").arg(method->name));
                continue;
            }
            info.methods.insert(method->name, method.get());
        }
        if (s->constructors.size() > 1)
            error(s->span, QStringLiteral("only one constructor is supported per struct in this v0 slice"));
        if (!s->constructors.empty())
            info.constructor = s->constructors.front().get();
        m_structs.insert(s->name, info);
    }
}

void TypeChecker::collectFunctions(const ProgramNode& program)
{
    for (const auto& decl : program.declarations) {
        if (auto* fn = dynamic_cast<FunctionDeclNode*>(decl.get())) {
            if (m_functions.contains(fn->name)) {
                error(fn->span, QStringLiteral("duplicate function '%1'").arg(fn->name));
                continue;
            }
            m_functions.insert(fn->name, fn);
        }
    }
}

void TypeChecker::collectBackends(const ProgramNode& program)
{
    for (const auto& decl : program.declarations) {
        auto* backend = dynamic_cast<BackendBlockNode*>(decl.get());
        if (!backend)
            continue;
        if (m_backends.contains(backend->name)) {
            error(backend->span, QStringLiteral("duplicate backend '%1'").arg(backend->name));
            continue;
        }
        BackendInfo info;
        info.decl = backend;
        for (const auto& fn : backend->functions) {
            if (info.functions.contains(fn->name)) {
                error(fn->span, QStringLiteral("duplicate backend function '%1::%2'").arg(backend->name, fn->name));
                continue;
            }
            info.functions.insert(fn->name, fn.get());
        }
        m_backends.insert(backend->name, info);
    }
}

void TypeChecker::checkStruct(const StructDeclNode& decl)
{
    for (const auto& field : decl.fields) {
        AbelType type = typeFromAst(*field->type);
        if (!isSupportedType(type))
            error(field->span, QStringLiteral("unsupported field type '%1'").arg(field->type->displayName()));
        if (type.isReference())
            error(field->span, QStringLiteral("reference fields are not supported in v0"));
    }
    for (const auto& ctor : decl.constructors)
        checkConstructor(decl, *ctor);
    for (const auto& method : decl.methods)
        checkMethod(decl, *method);
}

void TypeChecker::checkBackend(const BackendBlockNode& backend)
{
    for (const auto& fn : backend.functions) {
        const AbelType returnType = typeFromAst(*fn->returnType);
        if (!isSupportedType(returnType))
            error(fn->returnType->span, QStringLiteral("unsupported backend return type '%1'").arg(fn->returnType->displayName()));
        bool seenVariadic = false;
        for (size_t i = 0; i < fn->params.size(); ++i) {
            const ParameterNode& param = *fn->params[i];
            const AbelType paramType = typeFromAst(*param.type);
            if (!isSupportedType(paramType))
                error(param.span, QStringLiteral("unsupported backend parameter type '%1'").arg(param.type->displayName()));
            if (!param.variadic)
                continue;
            if (seenVariadic)
                error(param.span, QStringLiteral("only one variadic parameter is allowed"));
            seenVariadic = true;
            if (i + 1 != fn->params.size())
                error(param.span, QStringLiteral("variadic parameter must be last"));
            if (paramType.kind != TypeKind::Any)
                error(param.span, QStringLiteral("only any... variadic parameters are supported"));
        }
    }
}

void TypeChecker::checkConstructor(const StructDeclNode& owner, const ConstructorDeclNode& ctor)
{
    m_currentReturnType = makeType(TypeKind::Void);
    m_currentStruct = owner.name;
    pushScope();
    defineVariable(QStringLiteral("this"), makePointerType(makeStructType(owner.name)), false, owner.span);
    for (const auto& field : owner.fields)
        defineVariable(field->name, typeFromAst(*field->type), field->type->isConst, field->span);
    for (const auto& param : ctor.params) {
        AbelType paramType = typeFromAst(*param->type);
        if (!isSupportedType(paramType))
            error(param->span, QStringLiteral("unsupported parameter type '%1'").arg(param->type->displayName()));
        defineVariable(param->name, param->variadic ? makeVectorType(makeType(TypeKind::Any)) : paramType, false, param->span);
    }
    checkBlock(*ctor.body, false);
    popScope();
    m_currentStruct.clear();
}

void TypeChecker::checkMethod(const StructDeclNode& owner, const FunctionDeclNode& method)
{
    const AbelType returnType = typeFromAst(*method.returnType);
    if (!isSupportedType(returnType)) {
        error(method.returnType->span, QStringLiteral("unsupported return type '%1'").arg(method.returnType->displayName()));
        return;
    }
    m_currentReturnType = returnType;
    m_currentStruct = owner.name;
    pushScope();
    defineVariable(QStringLiteral("this"), makePointerType(makeStructType(owner.name)), method.isConstMethod, method.span);
    for (const auto& field : owner.fields)
        defineVariable(field->name, typeFromAst(*field->type), method.isConstMethod || field->type->isConst, field->span);
    for (const auto& param : method.params) {
        AbelType paramType = typeFromAst(*param->type);
        if (!isSupportedType(paramType))
            error(param->span, QStringLiteral("unsupported parameter type '%1'").arg(param->type->displayName()));
        defineVariable(param->name, param->variadic ? makeVectorType(makeType(TypeKind::Any)) : paramType, false, param->span);
    }
    if (method.body)
        checkBlock(*method.body, false);
    popScope();
    m_currentStruct.clear();
}

void TypeChecker::checkFunction(const FunctionDeclNode& fn)
{
    const AbelType returnType = typeFromAst(*fn.returnType);
    if (!isSupportedType(returnType)) {
        error(fn.returnType->span, QStringLiteral("unsupported return type '%1'").arg(fn.returnType->displayName()));
        return;
    }
    m_currentReturnType = returnType;

    pushScope();
    bool seenVariadic = false;
    for (size_t i = 0; i < fn.params.size(); ++i) {
        const ParameterNode& param = *fn.params[i];
        const AbelType paramType = typeFromAst(*param.type);
        if (!isSupportedType(paramType)) {
            error(param.span, QStringLiteral("unsupported parameter type '%1'").arg(param.type->displayName()));
            continue;
        }
        if (param.variadic) {
            if (seenVariadic)
                error(param.span, QStringLiteral("only one variadic parameter is allowed"));
            seenVariadic = true;
            if (i + 1 != fn.params.size())
                error(param.span, QStringLiteral("variadic parameter must be last"));
            if (paramType.kind != TypeKind::Any)
                error(param.span, QStringLiteral("only any... variadic parameters are supported"));
            defineVariable(param.name, makeVectorType(makeType(TypeKind::Any)), false, param.span);
        } else {
            defineVariable(param.name, paramType, false, param.span);
        }
    }

    if (!fn.debt && fn.body)
        checkBlock(*fn.body, false);
    popScope();
}

void TypeChecker::checkBlock(const BlockStmtNode& block, bool push)
{
    if (push)
        pushScope();
    for (const auto& stmt : block.statements)
        checkStmt(*stmt);
    if (push)
        popScope();
}

void TypeChecker::checkStmt(const StmtNode& stmt)
{
    if (auto* s = dynamic_cast<const ReturnStmtNode*>(&stmt)) {
        ExprType value = s->expr ? checkExpr(*s->expr) : ExprType{makeType(TypeKind::Void), ValueCategory::PRValue, false};
        if (!isUnknownType(value.type) && !isAssignable(m_currentReturnType, value.type))
            error(stmt.span,
                  QStringLiteral("cannot return %1 from function returning %2")
                      .arg(value.type.displayName(), m_currentReturnType.displayName()));
        return;
    }
    if (auto* s = dynamic_cast<const VarDeclStmtNode*>(&stmt)) {
        checkVarDecl(*s);
        return;
    }
    if (auto* s = dynamic_cast<const ExprStmtNode*>(&stmt)) {
        checkExpr(*s->expr);
        return;
    }
    if (auto* s = dynamic_cast<const BlockStmtNode*>(&stmt)) {
        checkBlock(*s, true);
        return;
    }
    if (auto* s = dynamic_cast<const IfStmtNode*>(&stmt)) {
        for (const auto& branch : s->branches) {
            if (branch.condition) {
                ExprType cond = checkExpr(*branch.condition);
                if (!isUnknownType(cond.type) && cond.type.kind != TypeKind::Bool)
                    error(branch.condition->span, QStringLiteral("if condition must be bool, got %1").arg(cond.type.displayName()));
            }
            checkBlock(*branch.body, true);
        }
        return;
    }
    if (auto* s = dynamic_cast<const WhileStmtNode*>(&stmt)) {
        ExprType cond = checkExpr(*s->condition);
        if (!isUnknownType(cond.type) && cond.type.kind != TypeKind::Bool)
            error(s->condition->span, QStringLiteral("while condition must be bool, got %1").arg(cond.type.displayName()));
        ++m_loopDepth;
        checkBlock(*s->body, true);
        --m_loopDepth;
        return;
    }
    if (auto* s = dynamic_cast<const RepeatStmtNode*>(&stmt)) {
        ExprType count = checkExpr(*s->count);
        if (!isUnknownType(count.type) && !count.type.isInteger())
            error(s->count->span, QStringLiteral("repeat count must be integer, got %1").arg(count.type.displayName()));
        ++m_loopDepth;
        checkBlock(*s->body, true);
        --m_loopDepth;
        return;
    }
    if (auto* s = dynamic_cast<const ForStmtNode*>(&stmt)) {
        checkFor(*s);
        return;
    }
    if (auto* s = dynamic_cast<const RangeForStmtNode*>(&stmt)) {
        checkRangeFor(*s);
        return;
    }
    if (dynamic_cast<const BreakStmtNode*>(&stmt) || dynamic_cast<const ContinueStmtNode*>(&stmt)) {
        if (m_loopDepth == 0)
            error(stmt.span, QStringLiteral("break/continue must be inside a loop"));
        return;
    }

    error(stmt.span, QStringLiteral("statement is not supported by the Stage 8 typechecker"));
}

void TypeChecker::checkVarDecl(const VarDeclStmtNode& stmt)
{
    const AbelType type = typeFromAst(*stmt.type);
    if (!isSupportedType(type)) {
        error(stmt.type->span, QStringLiteral("unsupported variable type '%1'").arg(stmt.type->displayName()));
        return;
    }

    if (type.isReference()) {
        if (!stmt.init) {
            error(stmt.span, QStringLiteral("reference variable '%1' must be initialized").arg(stmt.name));
            return;
        }
        ExprType init = checkExpr(*stmt.init);
        if (isUnknownType(init.type)) {
            // Root diagnostic already emitted by the initializer; do not add a reference-binding cascade.
        } else if (init.category != ValueCategory::LValue)
            error(stmt.init->span, QStringLiteral("reference variable '%1' must bind to an lvalue").arg(stmt.name));
        else if (!type.pointee || !isAssignable(*type.pointee, init.type))
            error(stmt.init->span,
                  QStringLiteral("cannot bind %1 to %2").arg(type.displayName(), init.type.displayName()));
        defineVariable(stmt.name, type, stmt.isConst || stmt.type->isConst, stmt.span);
        return;
    }

    if (stmt.init) {
        if (auto* initList = dynamic_cast<InitListExprNode*>(stmt.init.get())) {
            checkInitListAgainst(*initList, type);
        } else {
            ExprType init = checkExpr(*stmt.init);
            if (!isAssignable(type, init.type))
                error(stmt.init->span,
                      QStringLiteral("cannot initialize %1 with %2").arg(type.displayName(), init.type.displayName()));
        }
    } else if (!isDefaultConstructible(type)) {
        error(stmt.span, QStringLiteral("type %1 is not default-constructible").arg(type.displayName()));
    }
    defineVariable(stmt.name, type, stmt.isConst || stmt.type->isConst, stmt.span);
}

void TypeChecker::checkFor(const ForStmtNode& stmt)
{
    pushScope();
    if (stmt.init)
        checkStmt(*stmt.init);
    if (stmt.condition) {
        ExprType cond = checkExpr(*stmt.condition);
        if (!isUnknownType(cond.type) && cond.type.kind != TypeKind::Bool)
            error(stmt.condition->span, QStringLiteral("for condition must be bool, got %1").arg(cond.type.displayName()));
    }
    if (stmt.step)
        checkExpr(*stmt.step);
    ++m_loopDepth;
    checkBlock(*stmt.body, true);
    --m_loopDepth;
    popScope();
}

void TypeChecker::checkRangeFor(const RangeForStmtNode& stmt)
{
    ExprType range = checkExpr(*stmt.range);
    if (isUnknownType(range.type))
        return;
    if (range.type.kind != TypeKind::Vector || !range.type.pointee) {
        error(stmt.range->span, QStringLiteral("range-for requires vector, got %1").arg(range.type.displayName()));
        return;
    }

    pushScope();
    defineVariable(stmt.variable, makeReferenceType(*range.type.pointee), false, stmt.span);
    ++m_loopDepth;
    checkBlock(*stmt.body, true);
    --m_loopDepth;
    popScope();
}

ExprType TypeChecker::checkExpr(const ExprNode& expr)
{
    if (auto* e = dynamic_cast<const LiteralExprNode*>(&expr)) {
        switch (e->kind) {
        case LiteralExprNode::Kind::Int: return {makeType(TypeKind::I32), ValueCategory::PRValue, false};
        case LiteralExprNode::Kind::Float: return {makeType(TypeKind::F64), ValueCategory::PRValue, false};
        case LiteralExprNode::Kind::String: return {makeType(TypeKind::Str), ValueCategory::PRValue, false};
        case LiteralExprNode::Kind::Char: return {makeType(TypeKind::Char), ValueCategory::PRValue, false};
        case LiteralExprNode::Kind::Bool: return {makeType(TypeKind::Bool), ValueCategory::PRValue, false};
        case LiteralExprNode::Kind::Nullptr: return {makeType(TypeKind::Nullptr), ValueCategory::PRValue, false};
        }
    }
    if (auto* e = dynamic_cast<const NameExprNode*>(&expr)) return checkName(*e);
    if (auto* e = dynamic_cast<const UnaryExprNode*>(&expr)) return checkUnary(*e);
    if (auto* e = dynamic_cast<const BinaryExprNode*>(&expr)) return checkBinary(*e);
    if (auto* e = dynamic_cast<const CastExprNode*>(&expr)) return checkCast(*e);
    if (auto* e = dynamic_cast<const AssignExprNode*>(&expr)) return checkAssignment(*e);
    if (auto* e = dynamic_cast<const CallExprNode*>(&expr)) return checkCall(*e);
    if (auto* e = dynamic_cast<const LambdaExprNode*>(&expr)) return checkLambda(*e);
    if (auto* e = dynamic_cast<const FieldAccessExprNode*>(&expr)) return checkFieldAccess(*e);
    if (auto* e = dynamic_cast<const IndexExprNode*>(&expr)) return checkIndex(*e);
    if (dynamic_cast<const ThisExprNode*>(&expr)) {
        if (m_currentStruct.isEmpty())
            return errorExpr(expr.span, QStringLiteral("this is only available inside struct methods"));
        return {makePointerType(makeStructType(m_currentStruct)), ValueCategory::PRValue, false};
    }
    if (dynamic_cast<const InitListExprNode*>(&expr))
        return errorExpr(expr.span, QStringLiteral("initializer list needs a target type"));
    if (dynamic_cast<const StaticAccessExprNode*>(&expr))
        return errorExpr(expr.span, QStringLiteral("static/backend access is not typechecked in this stage"));

    return errorExpr(expr.span, QStringLiteral("expression is not supported by the Stage 8 typechecker"));
}

ExprType TypeChecker::checkName(const NameExprNode& expr)
{
    if (isSourceLocationBuiltinName(expr.name))
        return {sourceLocationBuiltinType(expr.name), ValueCategory::PRValue, false};

    const VariableInfo* var = lookupVariable(expr.name);
    if (!var)
        return errorExpr(expr.span, QStringLiteral("unknown variable '%1'").arg(expr.name));
    return {valueTypeOfVariable(var->type), ValueCategory::LValue, !var->isConst};
}

ExprType TypeChecker::checkUnary(const UnaryExprNode& expr)
{
    if (expr.op == QStringLiteral("&")) {
        ExprType inner = checkExpr(*expr.expr);
        if (isUnknownType(inner.type))
            return unknownExprType();
        if (inner.category != ValueCategory::LValue)
            return errorExpr(expr.span, QStringLiteral("address-of requires lvalue"));
        return {makePointerType(inner.type), ValueCategory::PRValue, false};
    }
    if (expr.op == QStringLiteral("*")) {
        ExprType inner = checkExpr(*expr.expr);
        if (isUnknownType(inner.type))
            return unknownExprType();
        if (!inner.type.isPointer() || !inner.type.pointee)
            return errorExpr(expr.span, QStringLiteral("dereference requires pointer"));
        return {*inner.type.pointee, ValueCategory::LValue, true};
    }
    ExprType inner = checkExpr(*expr.expr);
    if (isUnknownType(inner.type))
        return unknownExprType();
    if (expr.op == QStringLiteral("!")) {
        if (inner.type.kind != TypeKind::Bool)
            return errorExpr(expr.span, QStringLiteral("operator ! requires bool"));
        return {makeType(TypeKind::Bool), ValueCategory::PRValue, false};
    }
    if (expr.op == QStringLiteral("+") || expr.op == QStringLiteral("-")) {
        if (!inner.type.isNumeric())
            return errorExpr(expr.span, QStringLiteral("unary %1 requires numeric operand").arg(expr.op));
        return {inner.type, ValueCategory::PRValue, false};
    }
    return errorExpr(expr.span, QStringLiteral("unknown unary operator '%1'").arg(expr.op));
}

ExprType TypeChecker::checkBinary(const BinaryExprNode& expr)
{
    if (expr.op == QStringLiteral("|>"))
        return checkPipe(expr);

    ExprType lhs = checkExpr(*expr.lhs);
    ExprType rhs = checkExpr(*expr.rhs);
    const QString& op = expr.op;
    if (isUnknownType(lhs.type) || isUnknownType(rhs.type))
        return unknownExprType();

    if (op == QStringLiteral("&&") || op == QStringLiteral("||")) {
        if (lhs.type.kind != TypeKind::Bool || rhs.type.kind != TypeKind::Bool)
            return errorExpr(expr.span, QStringLiteral("logical operator '%1' requires bool operands").arg(op));
        return {makeType(TypeKind::Bool), ValueCategory::PRValue, false};
    }
    if (op == QStringLiteral("==") || op == QStringLiteral("!=")) {
        const bool ok = lhs.type == rhs.type
            || (lhs.type.isNumeric() && rhs.type.isNumeric())
            || (lhs.type.isPointer() && rhs.type.kind == TypeKind::Nullptr)
            || (lhs.type.kind == TypeKind::Nullptr && rhs.type.isPointer());
        if (!ok)
            return errorExpr(expr.span, QStringLiteral("cannot compare %1 and %2").arg(lhs.type.displayName(), rhs.type.displayName()));
        return {makeType(TypeKind::Bool), ValueCategory::PRValue, false};
    }
    if (op == QStringLiteral("+") && lhs.type.kind == TypeKind::Str && rhs.type.kind == TypeKind::Str)
        return {makeType(TypeKind::Str), ValueCategory::PRValue, false};

    if (!lhs.type.isNumeric() || !rhs.type.isNumeric())
        return errorExpr(expr.span, QStringLiteral("operator '%1' requires numeric operands").arg(op));

    if (op == QStringLiteral("<") || op == QStringLiteral("<=") || op == QStringLiteral(">") || op == QStringLiteral(">="))
        return {makeType(TypeKind::Bool), ValueCategory::PRValue, false};
    if (op == QStringLiteral("/") || op == QStringLiteral("*") || op == QStringLiteral("+") || op == QStringLiteral("-")
        || op == QStringLiteral("%") || op == QStringLiteral("%%") || op == QStringLiteral("**")
        || op == QStringLiteral("<?") || op == QStringLiteral(">?")) {
        if (lhs.type.kind == TypeKind::F64 || rhs.type.kind == TypeKind::F64)
            return {makeType(TypeKind::F64), ValueCategory::PRValue, false};
        if (lhs.type.kind == TypeKind::I64 || rhs.type.kind == TypeKind::I64)
            return {makeType(TypeKind::I64), ValueCategory::PRValue, false};
        return {makeType(TypeKind::I32), ValueCategory::PRValue, false};
    }

    return errorExpr(expr.span, QStringLiteral("unknown binary operator '%1'").arg(op));
}

ExprType TypeChecker::checkCast(const CastExprNode& expr)
{
    const AbelType target = typeFromAst(*expr.targetType);
    if (!isSupportedType(target))
        return errorExpr(expr.targetType->span, QStringLiteral("unsupported cast target type '%1'").arg(expr.targetType->displayName()));
    if (target.kind == TypeKind::Void || target.isReference())
        return errorExpr(expr.targetType->span, QStringLiteral("cast target must be a value type"));

    ExprType source = checkExpr(*expr.expr);
    if (isUnknownType(source.type))
        return unknownExprType();
    if (source.type.kind == TypeKind::Any)
        return {target, ValueCategory::PRValue, false};
    if (source.type.isNumeric() && target.isNumeric())
        return {target, ValueCategory::PRValue, false};
    if (isAssignable(target, source.type))
        return {target, ValueCategory::PRValue, false};
    return errorExpr(expr.expr->span,
                     QStringLiteral("cannot cast %1 to %2").arg(source.type.displayName(), target.displayName()));
}

ExprType TypeChecker::checkPipe(const BinaryExprNode& expr)
{
    ExprType lhs = checkExpr(*expr.lhs);
    if (auto* name = dynamic_cast<NameExprNode*>(expr.rhs.get()))
        return checkPipeTarget(name->name, name->span, lhs, {}, expr.span);

    if (auto* call = dynamic_cast<CallExprNode*>(expr.rhs.get())) {
        if (auto* name = dynamic_cast<NameExprNode*>(call->callee.get()))
            return checkPipeTarget(name->name, name->span, lhs, call->args, expr.span);
        return errorExpr(call->callee->span, QStringLiteral("pipe target call must use a named function"));
    }

    return errorExpr(expr.rhs->span, QStringLiteral("pipe right side must be f or f(args...)"));
}

ExprType TypeChecker::checkPipeTarget(const QString& name,
                                      const SourceSpan& nameSpan,
                                      const ExprType& lhs,
                                      const std::vector<std::unique_ptr<ExprNode>>& args,
                                      const SourceSpan& span)
{
    auto checkRestArgs = [&]() {
        for (const auto& argExpr : args)
            checkExpr(*argExpr);
    };

    if (const VariableInfo* variable = lookupVariable(name)) {
        const AbelType calleeType = valueTypeOfVariable(variable->type);
        if (calleeType.kind != TypeKind::Function)
            return errorExpr(nameSpan, QStringLiteral("pipe target variable '%1' is not a function value").arg(name));
        if (isUnknownType(lhs.type)) {
            checkRestArgs();
            return unknownExprType();
        }
        return checkFunctionValueCallShape(calleeType, lhs, args, span);
    }

    if (const FunctionDeclNode* fn = m_functions.value(name, nullptr)) {
        if (isUnknownType(lhs.type)) {
            checkRestArgs();
            return unknownExprType();
        }
        return checkFunctionCallShape(name, *fn, lhs, args, span);
    }

    if (m_builtins.hasFunction(name)) {
        if (isUnknownType(lhs.type)) {
            checkRestArgs();
            return unknownExprType();
        }
        const qsizetype argc = static_cast<qsizetype>(args.size()) + 1;
        if (name == QStringLiteral("to_str")) {
            if (argc != 1)
                return errorExpr(span, QStringLiteral("to_str expects one argument"));
            if (!isUnknownType(lhs.type) && !isStringifiable(lhs.type))
                return errorExpr(span, QStringLiteral("cannot stringify %1").arg(lhs.type.displayName()));
            return {makeType(TypeKind::Str), ValueCategory::PRValue, false};
        }
        if (name == QStringLiteral("str_to_chars")) {
            if (argc != 1)
                return errorExpr(span, QStringLiteral("str_to_chars expects one argument"));
            if (!isUnknownType(lhs.type) && lhs.type.kind != TypeKind::Str)
                return errorExpr(span, QStringLiteral("str_to_chars expects str, got %1").arg(lhs.type.displayName()));
            return {makeVectorType(makeType(TypeKind::Char)), ValueCategory::PRValue, false};
        }
        if (name == QStringLiteral("chars_to_str")) {
            if (argc != 1)
                return errorExpr(span, QStringLiteral("chars_to_str expects one argument"));
            const AbelType charVector = makeVectorType(makeType(TypeKind::Char));
            if (!isUnknownType(lhs.type) && !isAssignable(charVector, lhs.type))
                return errorExpr(span, QStringLiteral("chars_to_str expects vector<char>, got %1").arg(lhs.type.displayName()));
            return {makeType(TypeKind::Str), ValueCategory::PRValue, false};
        }
        if (name == QStringLiteral("build_string")) {
            if (!isUnknownType(lhs.type) && !isStringifiable(lhs.type))
                error(span, QStringLiteral("cannot stringify %1").arg(lhs.type.displayName()));
            for (const auto& argExpr : args) {
                ExprType arg = checkExpr(*argExpr);
                if (!isUnknownType(arg.type) && !isStringifiable(arg.type))
                    error(argExpr->span, QStringLiteral("cannot stringify %1").arg(arg.type.displayName()));
            }
            return {makeType(TypeKind::Str), ValueCategory::PRValue, false};
        }
        if (name == QStringLiteral("print") || name == QStringLiteral("println")) {
            if (!isUnknownType(lhs.type) && !isStringifiable(lhs.type))
                error(span, QStringLiteral("cannot stringify %1").arg(lhs.type.displayName()));
            for (const auto& argExpr : args) {
                ExprType arg = checkExpr(*argExpr);
                if (!isUnknownType(arg.type) && !isStringifiable(arg.type))
                    error(argExpr->span, QStringLiteral("cannot stringify %1").arg(arg.type.displayName()));
            }
            return {makeType(TypeKind::Void), ValueCategory::PRValue, false};
        }
        if (name == QStringLiteral("debug_break"))
            return errorExpr(span, QStringLiteral("debug_break expects no arguments"));
        if (name == QStringLiteral("debug_assert")) {
            if (!isUnknownType(lhs.type) && lhs.type.kind != TypeKind::Bool)
                error(span, QStringLiteral("debug_assert condition must be bool, got %1").arg(lhs.type.displayName()));
            for (const auto& argExpr : args) {
                ExprType arg = checkExpr(*argExpr);
                if (!isUnknownType(arg.type) && !isStringifiable(arg.type))
                    error(argExpr->span, QStringLiteral("cannot stringify %1").arg(arg.type.displayName()));
            }
            return {makeType(TypeKind::Void), ValueCategory::PRValue, false};
        }
    }

    return errorExpr(nameSpan, QStringLiteral("unknown pipe target '%1'").arg(name));
}

ExprType TypeChecker::checkFunctionCallShape(const QString& name,
                                             const FunctionDeclNode& fn,
                                             const ExprType& firstArg,
                                             const std::vector<std::unique_ptr<ExprNode>>& restArgs,
                                             const SourceSpan& span)
{
    const bool variadic = !fn.params.empty() && fn.params.back()->variadic;
    const size_t fixedCount = variadic ? fn.params.size() - 1 : fn.params.size();
    const size_t argc = restArgs.size() + 1;
    if ((!variadic && argc != fn.params.size()) || (variadic && argc < fixedCount))
        return errorExpr(span, QStringLiteral("function '%1' called with wrong argument count").arg(name));

    auto checkOne = [&](size_t i, const ExprType& arg, const SourceSpan& argSpan) {
        if (isUnknownType(arg.type))
            return;
        const ParameterNode& param = *fn.params[i];
        const AbelType paramType = typeFromAst(*param.type);
        if (paramType.isReference()) {
            if (arg.category != ValueCategory::LValue)
                error(argSpan, QStringLiteral("reference parameter '%1' requires lvalue").arg(param.name));
            else if (!paramType.pointee || !isAssignable(*paramType.pointee, arg.type))
                error(argSpan, QStringLiteral("cannot bind reference parameter '%1'").arg(param.name));
        } else if (!isAssignable(paramType, arg.type)) {
            error(argSpan,
                  QStringLiteral("cannot pass %1 to parameter '%2' of type %3")
                      .arg(arg.type.displayName(), param.name, paramType.displayName()));
        }
    };

    for (size_t i = 0; i < fixedCount; ++i) {
        if (i == 0) {
            checkOne(i, firstArg, span);
        } else {
            ExprType arg = checkExpr(*restArgs[i - 1]);
            checkOne(i, arg, restArgs[i - 1]->span);
        }
    }
    for (size_t i = fixedCount; i < argc; ++i) {
        if (i > 0)
            checkExpr(*restArgs[i - 1]);
    }
    return {typeFromAst(*fn.returnType), ValueCategory::PRValue, false};
}

ExprType TypeChecker::checkFunctionValueCallShape(const AbelType& functionType,
                                                  const ExprType& firstArg,
                                                  const std::vector<std::unique_ptr<ExprNode>>& restArgs,
                                                  const SourceSpan& span)
{
    if (isUnknownType(functionType)) {
        for (const auto& argExpr : restArgs)
            checkExpr(*argExpr);
        return unknownExprType();
    }
    if (functionType.kind != TypeKind::Function || !functionType.pointee)
        return errorExpr(span, QStringLiteral("callee is not a function value"));
    if (restArgs.size() + 1 != functionType.params.size())
        return errorExpr(span, QStringLiteral("function value called with wrong argument count"));

    auto checkOne = [&](size_t i, const ExprType& arg, const SourceSpan& argSpan) {
        if (isUnknownType(arg.type))
            return;
        const AbelType& paramType = functionType.params[i];
        if (paramType.isReference()) {
            if (arg.category != ValueCategory::LValue)
                error(argSpan, QStringLiteral("reference function parameter requires lvalue"));
            else if (!paramType.pointee || !isAssignable(*paramType.pointee, arg.type))
                error(argSpan,
                      QStringLiteral("cannot bind %1 to %2 lvalue").arg(paramType.displayName(), arg.type.displayName()));
        } else if (!isAssignable(paramType, arg.type)) {
            error(argSpan,
                  QStringLiteral("cannot pass %1 to function parameter %2")
                      .arg(arg.type.displayName(), paramType.displayName()));
        }
    };

    checkOne(0, firstArg, span);
    for (size_t i = 1; i < functionType.params.size(); ++i) {
        ExprType arg = checkExpr(*restArgs[i - 1]);
        checkOne(i, arg, restArgs[i - 1]->span);
    }
    return {*functionType.pointee, ValueCategory::PRValue, false};
}

ExprType TypeChecker::checkAssignment(const AssignExprNode& expr)
{
    ExprType lhs = checkExpr(*expr.lhs);
    ExprType rhs = checkExpr(*expr.rhs);
    if (isUnknownType(lhs.type) || isUnknownType(rhs.type))
        return unknownExprType();
    if (lhs.category != ValueCategory::LValue)
        return errorExpr(expr.span, QStringLiteral("left side of assignment must be an lvalue"));
    if (!lhs.isMutable)
        return errorExpr(expr.span, QStringLiteral("cannot assign to const lvalue"));
    if (!isAssignable(lhs.type, rhs.type))
        return errorExpr(expr.span, QStringLiteral("cannot assign %1 to %2").arg(rhs.type.displayName(), lhs.type.displayName()));
    return {lhs.type, ValueCategory::PRValue, false};
}

ExprType TypeChecker::checkCall(const CallExprNode& expr)
{
    if (auto* access = dynamic_cast<StaticAccessExprNode*>(expr.callee.get()))
        return checkBackendCall(*access, expr.args, expr.span);

    if (auto* field = dynamic_cast<FieldAccessExprNode*>(expr.callee.get())) {
        ExprType receiver = checkExpr(*field->base);
        if (isUnknownType(receiver.type)) {
            for (const auto& argExpr : expr.args)
                checkExpr(*argExpr);
            return unknownExprType();
        }
        if (receiver.type.kind == TypeKind::Struct) {
            const StructInfo info = m_structs.value(receiver.type.spelling);
            const FunctionDeclNode* method = info.methods.value(field->field, nullptr);
            if (!method)
                return errorExpr(field->span, QStringLiteral("unknown method '%1' on %2").arg(field->field, receiver.type.displayName()));
            if (expr.args.size() != method->params.size())
                return errorExpr(expr.span, QStringLiteral("method '%1' called with wrong argument count").arg(field->field));
            for (size_t i = 0; i < expr.args.size(); ++i) {
                AbelType paramType = typeFromAst(*method->params[i]->type);
                ExprType arg = checkExpr(*expr.args[i]);
                if (!isUnknownType(arg.type) && !isAssignable(paramType, arg.type))
                    error(expr.args[i]->span, QStringLiteral("cannot pass %1 to method parameter %2").arg(arg.type.displayName(), paramType.displayName()));
            }
            return {typeFromAst(*method->returnType), method->returnType->isReference ? ValueCategory::LValue : ValueCategory::PRValue, false};
        }
        return checkBuiltinMethodCall(*field, expr.args);
    }

    auto* name = dynamic_cast<NameExprNode*>(expr.callee.get());
    if (!name)
        return checkFunctionValueCall(checkExpr(*expr.callee).type, expr.args, expr.span);

    if (const VariableInfo* variable = lookupVariable(name->name)) {
        const AbelType calleeType = valueTypeOfVariable(variable->type);
        if (calleeType.kind != TypeKind::Function)
            return errorExpr(expr.span, QStringLiteral("variable '%1' is not a function value").arg(name->name));
        return checkFunctionValueCall(calleeType, expr.args, expr.span);
    }

    if (const StructInfo* info = m_structs.contains(name->name) ? &m_structs[name->name] : nullptr) {
        const size_t argc = expr.args.size();
        if (!info->constructor) {
            if (argc == 0) {
                if (!isDefaultConstructible(makeStructType(name->name)))
                    return errorExpr(expr.span, QStringLiteral("constructor '%1' is not default-constructible").arg(name->name));
            } else {
                if (argc != info->fields.size())
                    return errorExpr(expr.span, QStringLiteral("constructor '%1' expects 0 or %2 argument(s)").arg(name->name).arg(info->fields.size()));
                for (size_t i = 0; i < argc; ++i) {
                    const QString& fieldName = info->decl->fields[i]->name;
                    ExprType arg = checkExpr(*expr.args[i]);
                    if (!isUnknownType(arg.type) && !isAssignable(info->fields.value(fieldName).type, arg.type))
                        error(expr.args[i]->span, QStringLiteral("cannot initialize field '%1'").arg(fieldName));
                }
            }
        } else {
            if (argc != info->constructor->params.size())
                return errorExpr(expr.span, QStringLiteral("constructor '%1' called with wrong argument count").arg(name->name));
            for (size_t i = 0; i < argc; ++i) {
                AbelType paramType = typeFromAst(*info->constructor->params[i]->type);
                ExprType arg = checkExpr(*expr.args[i]);
                if (!isUnknownType(arg.type) && !isAssignable(paramType, arg.type))
                    error(expr.args[i]->span, QStringLiteral("cannot pass %1 to constructor parameter %2").arg(arg.type.displayName(), paramType.displayName()));
            }
        }
        return {makeStructType(name->name), ValueCategory::PRValue, false};
    }

    if (const FunctionDeclNode* fn = m_functions.value(name->name, nullptr)) {
        const bool variadic = !fn->params.empty() && fn->params.back()->variadic;
        const size_t fixedCount = variadic ? fn->params.size() - 1 : fn->params.size();
        if ((!variadic && expr.args.size() != fn->params.size()) || (variadic && expr.args.size() < fixedCount))
            return errorExpr(expr.span, QStringLiteral("function '%1' called with wrong argument count").arg(name->name));
        for (size_t i = 0; i < fixedCount; ++i) {
            const ParameterNode& param = *fn->params[i];
            const AbelType paramType = typeFromAst(*param.type);
            ExprType arg = checkExpr(*expr.args[i]);
            if (isUnknownType(arg.type))
                continue;
            if (paramType.isReference()) {
                if (arg.category != ValueCategory::LValue)
                    error(expr.args[i]->span, QStringLiteral("reference parameter '%1' requires lvalue").arg(param.name));
                else if (!paramType.pointee || !isAssignable(*paramType.pointee, arg.type))
                    error(expr.args[i]->span, QStringLiteral("cannot bind reference parameter '%1'").arg(param.name));
            } else if (!isAssignable(paramType, arg.type)) {
                error(expr.args[i]->span,
                      QStringLiteral("cannot pass %1 to parameter '%2' of type %3")
                          .arg(arg.type.displayName(), param.name, paramType.displayName()));
            }
        }
        for (size_t i = fixedCount; i < expr.args.size(); ++i)
            checkExpr(*expr.args[i]);
        return {typeFromAst(*fn->returnType), ValueCategory::PRValue, false};
    }

    if (m_builtins.hasFunction(name->name)) {
        const qsizetype argc = static_cast<qsizetype>(expr.args.size());
        if (name->name == QStringLiteral("to_str")) {
            if (argc != 1)
                return errorExpr(expr.span, QStringLiteral("to_str expects one argument"));
            ExprType arg = checkExpr(*expr.args[0]);
            if (!isUnknownType(arg.type) && !isStringifiable(arg.type))
                return errorExpr(expr.args[0]->span, QStringLiteral("cannot stringify %1").arg(arg.type.displayName()));
            return {makeType(TypeKind::Str), ValueCategory::PRValue, false};
        }
        if (name->name == QStringLiteral("str_to_chars")) {
            if (argc != 1)
                return errorExpr(expr.span, QStringLiteral("str_to_chars expects one argument"));
            ExprType arg = checkExpr(*expr.args[0]);
            if (!isUnknownType(arg.type) && arg.type.kind != TypeKind::Str)
                return errorExpr(expr.args[0]->span, QStringLiteral("str_to_chars expects str, got %1").arg(arg.type.displayName()));
            return {makeVectorType(makeType(TypeKind::Char)), ValueCategory::PRValue, false};
        }
        if (name->name == QStringLiteral("chars_to_str")) {
            if (argc != 1)
                return errorExpr(expr.span, QStringLiteral("chars_to_str expects one argument"));
            ExprType arg = checkExpr(*expr.args[0]);
            const AbelType charVector = makeVectorType(makeType(TypeKind::Char));
            if (!isUnknownType(arg.type) && !isAssignable(charVector, arg.type))
                return errorExpr(expr.args[0]->span, QStringLiteral("chars_to_str expects vector<char>, got %1").arg(arg.type.displayName()));
            return {makeType(TypeKind::Str), ValueCategory::PRValue, false};
        }
        if (name->name == QStringLiteral("build_string")) {
            for (const auto& argExpr : expr.args) {
                ExprType arg = checkExpr(*argExpr);
                if (!isUnknownType(arg.type) && !isStringifiable(arg.type))
                    error(argExpr->span, QStringLiteral("cannot stringify %1").arg(arg.type.displayName()));
            }
            return {makeType(TypeKind::Str), ValueCategory::PRValue, false};
        }
        if (name->name == QStringLiteral("print") || name->name == QStringLiteral("println")) {
            for (const auto& argExpr : expr.args) {
                ExprType arg = checkExpr(*argExpr);
                if (!isUnknownType(arg.type) && !isStringifiable(arg.type))
                    error(argExpr->span, QStringLiteral("cannot stringify %1").arg(arg.type.displayName()));
            }
            return {makeType(TypeKind::Void), ValueCategory::PRValue, false};
        }
        if (name->name == QStringLiteral("debug_break")) {
            if (argc != 0)
                return errorExpr(expr.span, QStringLiteral("debug_break expects no arguments"));
            return {makeType(TypeKind::Void), ValueCategory::PRValue, false};
        }
        if (name->name == QStringLiteral("debug_assert")) {
            if (argc < 1)
                return errorExpr(expr.span, QStringLiteral("debug_assert expects at least one argument"));
            ExprType cond = checkExpr(*expr.args[0]);
            if (!isUnknownType(cond.type) && cond.type.kind != TypeKind::Bool)
                error(expr.args[0]->span, QStringLiteral("debug_assert condition must be bool, got %1").arg(cond.type.displayName()));
            for (size_t i = 1; i < expr.args.size(); ++i) {
                ExprType arg = checkExpr(*expr.args[i]);
                if (!isUnknownType(arg.type) && !isStringifiable(arg.type))
                    error(expr.args[i]->span, QStringLiteral("cannot stringify %1").arg(arg.type.displayName()));
            }
            return {makeType(TypeKind::Void), ValueCategory::PRValue, false};
        }
    }

    return errorExpr(expr.span, QStringLiteral("unknown function '%1'").arg(name->name));
}

ExprType TypeChecker::checkBackendCall(const StaticAccessExprNode& callee,
                                       const std::vector<std::unique_ptr<ExprNode>>& args,
                                       const SourceSpan& span)
{
    auto* backendName = dynamic_cast<NameExprNode*>(callee.base.get());
    if (!backendName)
        return errorExpr(callee.span, QStringLiteral("backend call receiver must be a backend name"));
    const BackendInfo* backend = m_backends.contains(backendName->name) ? &m_backends[backendName->name] : nullptr;
    if (!backend)
        return errorExpr(callee.span, QStringLiteral("unknown backend '%1'").arg(backendName->name));
    const FunctionDeclNode* fn = backend->functions.value(callee.member, nullptr);
    if (!fn)
        return errorExpr(callee.span, QStringLiteral("unknown backend function '%1::%2'").arg(backendName->name, callee.member));

    const bool variadic = !fn->params.empty() && fn->params.back()->variadic;
    const size_t fixedCount = variadic ? fn->params.size() - 1 : fn->params.size();
    if ((!variadic && args.size() != fn->params.size()) || (variadic && args.size() < fixedCount))
        return errorExpr(span, QStringLiteral("backend function '%1::%2' called with wrong argument count").arg(backendName->name, callee.member));

    for (size_t i = 0; i < fixedCount; ++i) {
        const ParameterNode& param = *fn->params[i];
        const AbelType paramType = typeFromAst(*param.type);
        ExprType arg = checkExpr(*args[i]);
        if (isUnknownType(arg.type))
            continue;
        if (paramType.isReference()) {
            if (arg.category != ValueCategory::LValue)
                error(args[i]->span, QStringLiteral("backend reference parameter '%1' requires lvalue").arg(param.name));
            else if (!paramType.pointee || !isAssignable(*paramType.pointee, arg.type))
                error(args[i]->span, QStringLiteral("cannot bind backend reference parameter '%1'").arg(param.name));
        } else if (!isAssignable(paramType, arg.type)) {
            error(args[i]->span,
                  QStringLiteral("cannot pass %1 to backend parameter '%2' of type %3")
                      .arg(arg.type.displayName(), param.name, paramType.displayName()));
        }
    }
    for (size_t i = fixedCount; i < args.size(); ++i)
        checkExpr(*args[i]);

    const AbelType returnType = typeFromAst(*fn->returnType);
    return {returnType, returnType.isReference() ? ValueCategory::LValue : ValueCategory::PRValue, false};
}

ExprType TypeChecker::checkFunctionValueCall(const AbelType& functionType,
                                             const std::vector<std::unique_ptr<ExprNode>>& args,
                                             const SourceSpan& span)
{
    if (isUnknownType(functionType)) {
        for (const auto& argExpr : args)
            checkExpr(*argExpr);
        return unknownExprType();
    }
    if (functionType.kind != TypeKind::Function || !functionType.pointee)
        return errorExpr(span, QStringLiteral("callee is not a function value"));
    if (args.size() != functionType.params.size())
        return errorExpr(span, QStringLiteral("function value called with wrong argument count"));
    for (size_t i = 0; i < args.size(); ++i) {
        const AbelType& paramType = functionType.params[i];
        ExprType arg = checkExpr(*args[i]);
        if (isUnknownType(arg.type))
            continue;
        if (paramType.isReference()) {
            if (arg.category != ValueCategory::LValue)
                error(args[i]->span, QStringLiteral("reference function parameter requires lvalue"));
            else if (!paramType.pointee || !isAssignable(*paramType.pointee, arg.type))
                error(args[i]->span,
                      QStringLiteral("cannot bind %1 to %2 lvalue").arg(paramType.displayName(), arg.type.displayName()));
        } else if (!isAssignable(paramType, arg.type)) {
            error(args[i]->span,
                  QStringLiteral("cannot pass %1 to function parameter %2")
                      .arg(arg.type.displayName(), paramType.displayName()));
        }
    }
    return {*functionType.pointee, ValueCategory::PRValue, false};
}

ExprType TypeChecker::checkLambda(const LambdaExprNode& expr)
{
    AbelType returnType = typeFromAst(*expr.returnType);
    if (!isSupportedType(returnType))
        error(expr.returnType->span, QStringLiteral("unsupported lambda return type '%1'").arg(expr.returnType->displayName()));
    std::vector<AbelType> params;
    params.reserve(expr.paramTypes.size());
    for (const auto& type : expr.paramTypes) {
        AbelType paramType = typeFromAst(*type);
        if (!isSupportedType(paramType))
            error(type->span, QStringLiteral("unsupported lambda parameter type '%1'").arg(type->displayName()));
        params.push_back(paramType);
    }

    QHash<QString, VariableInfo> visible;
    for (const auto& scope : m_scopes) {
        for (auto it = scope.constBegin(); it != scope.constEnd(); ++it)
            visible.insert(it.key(), it.value());
    }

    enum class CaptureMode {
        Value,
        Reference,
    };
    std::optional<CaptureMode> defaultMode;
    QHash<QString, CaptureMode> explicitCaptures;
    const QStringList captureList = expr.captureText.split(QStringLiteral(","), Qt::SkipEmptyParts);
    for (const QString& raw : captureList) {
        const QString capture = raw.trimmed();
        if (capture == QStringLiteral("=")) {
            defaultMode = CaptureMode::Value;
        } else if (capture == QStringLiteral("&")) {
            defaultMode = CaptureMode::Reference;
        } else if (capture.startsWith(QStringLiteral("&"))) {
            explicitCaptures.insert(capture.mid(1), CaptureMode::Reference);
        } else {
            explicitCaptures.insert(capture, CaptureMode::Value);
        }
    }

    QHash<QString, VariableInfo> captures;
    QSet<QString> capturedNames;
    auto captureOne = [&](const QString& name, CaptureMode mode) {
        if (capturedNames.contains(name))
            return;
        auto found = visible.constFind(name);
        if (found == visible.constEnd()) {
            error(expr.span, QStringLiteral("unknown lambda capture '%1'").arg(name));
            return;
        }
        capturedNames.insert(name);
        captures.insert(name, VariableInfo{valueTypeOfVariable(found.value().type), mode == CaptureMode::Value || found.value().isConst});
    };

    if (defaultMode.has_value()) {
        for (auto it = visible.constBegin(); it != visible.constEnd(); ++it) {
            if (!explicitCaptures.contains(it.key()))
                captureOne(it.key(), *defaultMode);
        }
    }
    for (auto it = explicitCaptures.constBegin(); it != explicitCaptures.constEnd(); ++it)
        captureOne(it.key(), it.value());

    const AbelType previousReturn = m_currentReturnType;
    const auto previousScopes = m_scopes;
    const int previousLoopDepth = m_loopDepth;
    m_currentReturnType = returnType;
    m_loopDepth = 0;
    m_scopes.clear();
    pushScope();
    for (auto it = captures.constBegin(); it != captures.constEnd(); ++it)
        defineVariable(it.key(), it.value().type, it.value().isConst, expr.span);
    pushScope();
    for (size_t i = 0; i < params.size(); ++i)
        defineVariable(expr.paramNames[static_cast<qsizetype>(i)], params[i], false, expr.span);
    checkBlock(*expr.ownedBody, false);
    popScope();
    popScope();
    m_scopes = previousScopes;
    m_loopDepth = previousLoopDepth;
    m_currentReturnType = previousReturn;

    return {makeFunctionType(returnType, std::move(params)), ValueCategory::PRValue, false};
}

ExprType TypeChecker::checkBuiltinMethodCall(const FieldAccessExprNode& callee, const std::vector<std::unique_ptr<ExprNode>>& args)
{
    ExprType receiver = checkExpr(*callee.base);
    if (isUnknownType(receiver.type)) {
        for (const auto& argExpr : args)
            checkExpr(*argExpr);
        return unknownExprType();
    }
    if (receiver.type.kind != TypeKind::Vector || !receiver.type.pointee)
        return errorExpr(callee.span, QStringLiteral("builtin method '%1' requires vector receiver").arg(callee.field));
    if (!m_builtins.hasMethod(receiver.type, callee.field))
        return errorExpr(callee.span, QStringLiteral("unknown builtin method '%1'").arg(callee.field));

    const AbelType& element = *receiver.type.pointee;
    if (callee.field == QStringLiteral("len")) {
        if (!args.empty()) return errorExpr(callee.span, QStringLiteral("vector.len expects no arguments"));
        return {makeType(TypeKind::I32), ValueCategory::PRValue, false};
    }
    if (callee.field == QStringLiteral("empty")) {
        if (!args.empty()) return errorExpr(callee.span, QStringLiteral("vector.empty expects no arguments"));
        return {makeType(TypeKind::Bool), ValueCategory::PRValue, false};
    }
    if (callee.field == QStringLiteral("push")) {
        if (args.size() != 1) return errorExpr(callee.span, QStringLiteral("vector.push expects one argument"));
        if (receiver.category == ValueCategory::LValue && !receiver.isMutable)
            return errorExpr(callee.span, QStringLiteral("vector.push requires mutable vector receiver"));
        ExprType arg = checkExpr(*args[0]);
        if (!isUnknownType(arg.type) && !isAssignable(element, arg.type))
            error(args[0]->span, QStringLiteral("cannot push %1 into vector<%2>").arg(arg.type.displayName(), element.displayName()));
        return {makeType(TypeKind::Void), ValueCategory::PRValue, false};
    }
    if (callee.field == QStringLiteral("pop") || callee.field == QStringLiteral("clear")) {
        if (!args.empty())
            return errorExpr(callee.span, QStringLiteral("vector.%1 expects no arguments").arg(callee.field));
        if (receiver.category == ValueCategory::LValue && !receiver.isMutable)
            return errorExpr(callee.span, QStringLiteral("vector.%1 requires mutable vector receiver").arg(callee.field));
        return callee.field == QStringLiteral("pop")
            ? ExprType{element, ValueCategory::PRValue, false}
            : ExprType{makeType(TypeKind::Void), ValueCategory::PRValue, false};
    }
    if (callee.field == QStringLiteral("reserve") || callee.field == QStringLiteral("resize")) {
        if (args.size() != 1)
            return errorExpr(callee.span, QStringLiteral("vector.%1 expects one argument").arg(callee.field));
        if (receiver.category == ValueCategory::LValue && !receiver.isMutable)
            return errorExpr(callee.span, QStringLiteral("vector.%1 requires mutable vector receiver").arg(callee.field));
        ExprType count = checkExpr(*args[0]);
        if (!isUnknownType(count.type) && !isAssignable(makeType(TypeKind::I64), count.type))
            error(args[0]->span, QStringLiteral("vector.%1 size must be integer").arg(callee.field));
        if (callee.field == QStringLiteral("resize") && !isDefaultConstructible(element))
            error(callee.span, QStringLiteral("vector.resize requires default-constructible element type %1").arg(element.displayName()));
        return {makeType(TypeKind::Void), ValueCategory::PRValue, false};
    }
    if (callee.field == QStringLiteral("front") || callee.field == QStringLiteral("back")) {
        if (!args.empty())
            return errorExpr(callee.span, QStringLiteral("vector.%1 expects no arguments").arg(callee.field));
        return {element, ValueCategory::LValue, receiver.category == ValueCategory::PRValue || receiver.isMutable};
    }
    return errorExpr(callee.span, QStringLiteral("unsupported builtin method '%1'").arg(callee.field));
}

ExprType TypeChecker::checkIndex(const IndexExprNode& expr)
{
    ExprType base = checkExpr(*expr.base);
    ExprType index = checkExpr(*expr.index);
    if (isUnknownType(base.type))
        return unknownExprType();
    if (base.type.kind != TypeKind::Vector || !base.type.pointee)
        return errorExpr(expr.span, QStringLiteral("indexing requires vector"));
    if (!isUnknownType(index.type) && !index.type.isInteger())
        error(expr.index->span, QStringLiteral("vector index must be integer"));
    return {*base.type.pointee, ValueCategory::LValue, base.category == ValueCategory::PRValue || base.isMutable};
}

ExprType TypeChecker::checkInitListAgainst(const InitListExprNode& init, const AbelType& target)
{
    if (target.kind != TypeKind::Vector || !target.pointee)
        return errorExpr(init.span, QStringLiteral("initializer list requires vector target"));
    for (const auto& value : init.values) {
        ExprType element = checkExpr(*value);
        if (!isUnknownType(element.type) && !isAssignable(*target.pointee, element.type))
            error(value->span, QStringLiteral("cannot put %1 into %2").arg(element.type.displayName(), target.displayName()));
    }
    return {target, ValueCategory::PRValue, false};
}

void TypeChecker::pushScope()
{
    m_scopes.push_back({});
}

void TypeChecker::popScope()
{
    if (!m_scopes.isEmpty())
        m_scopes.pop_back();
}

void TypeChecker::defineVariable(const QString& name, const AbelType& type, bool isConst, const SourceSpan& span)
{
    if (m_scopes.isEmpty())
        pushScope();
    auto& scope = m_scopes.back();
    if (scope.contains(name)) {
        error(span, QStringLiteral("variable '%1' is already defined in this scope").arg(name));
        return;
    }
    scope.insert(name, VariableInfo{type, isConst});
}

const TypeChecker::VariableInfo* TypeChecker::lookupVariable(const QString& name) const
{
    for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
        auto found = it->constFind(name);
        if (found != it->constEnd())
            return &found.value();
    }
    return nullptr;
}

ExprType TypeChecker::checkFieldAccess(const FieldAccessExprNode& expr)
{
    ExprType base = checkExpr(*expr.base);
    if (isUnknownType(base.type))
        return unknownExprType();
    AbelType objectType = base.type;
    bool mutableBase = base.isMutable;
    if (expr.pointer) {
        if (!objectType.isPointer() || !objectType.pointee)
            return errorExpr(expr.span, QStringLiteral("operator -> requires pointer receiver"));
        objectType = *objectType.pointee;
    }
    if (isUnknownType(objectType))
        return unknownExprType();
    if (objectType.kind != TypeKind::Struct)
        return errorExpr(expr.span, QStringLiteral("field access requires struct receiver"));
    const StructInfo info = m_structs.value(objectType.spelling);
    if (!info.fields.contains(expr.field))
        return errorExpr(expr.span, QStringLiteral("unknown field '%1' on %2").arg(expr.field, objectType.displayName()));
    const FieldInfo field = info.fields.value(expr.field);
    return {field.type, ValueCategory::LValue, mutableBase && !field.isConst};
}

bool TypeChecker::isSupportedType(const AbelType& type) const
{
    if (type.kind == TypeKind::Unknown || type.kind == TypeKind::Nullptr)
        return false;
    if (type.kind == TypeKind::Struct)
        return m_structs.contains(type.spelling);
    if (type.kind == TypeKind::Function) {
        if (!type.pointee || !isSupportedType(*type.pointee))
            return false;
        for (const auto& param : type.params) {
            if (!isSupportedType(param))
                return false;
        }
        return true;
    }
    if ((type.kind == TypeKind::Pointer || type.kind == TypeKind::Reference || type.kind == TypeKind::Vector) && type.pointee)
        return isSupportedType(*type.pointee);
    return true;
}

bool TypeChecker::isAssignable(const AbelType& target, const AbelType& source) const
{
    return canAssignValue(target, source);
}

bool TypeChecker::isDefaultConstructible(const AbelType& type) const
{
    QSet<QString> visiting;
    return isDefaultConstructible(type, visiting);
}

bool TypeChecker::isDefaultConstructible(const AbelType& type, QSet<QString>& visiting) const
{
    switch (type.kind) {
    case TypeKind::Void:
    case TypeKind::Bool:
    case TypeKind::I32:
    case TypeKind::I64:
    case TypeKind::F64:
    case TypeKind::Char:
    case TypeKind::Str:
    case TypeKind::Any:
    case TypeKind::Nullptr:
    case TypeKind::Pointer:
    case TypeKind::Vector:
        return true;
    case TypeKind::Reference:
    case TypeKind::Function:
    case TypeKind::Unknown:
        return false;
    case TypeKind::Struct: {
        const StructInfo info = m_structs.value(type.spelling);
        if (!info.decl)
            return false;
        if (visiting.contains(type.spelling))
            return false;
        if (info.constructor)
            return info.constructor->params.empty();

        visiting.insert(type.spelling);
        for (const auto& field : info.decl->fields) {
            if (!isDefaultConstructible(typeFromAst(*field->type), visiting)) {
                visiting.remove(type.spelling);
                return false;
            }
        }
        visiting.remove(type.spelling);
        return true;
    }
    }
    return false;
}

bool TypeChecker::isStringifiable(const AbelType& type) const
{
    switch (type.kind) {
    case TypeKind::Void:
    case TypeKind::Bool:
    case TypeKind::I32:
    case TypeKind::I64:
    case TypeKind::F64:
    case TypeKind::Char:
    case TypeKind::Str:
    case TypeKind::Any:
    case TypeKind::Pointer:
    case TypeKind::Nullptr:
        return true;
    case TypeKind::Vector:
        return type.pointee && isStringifiable(*type.pointee);
    case TypeKind::Reference:
        return type.pointee && isStringifiable(*type.pointee);
    case TypeKind::Struct:
        return hasUserToStrFor(type);
    case TypeKind::Function:
    case TypeKind::Unknown:
        return false;
    }
    return false;
}

bool TypeChecker::hasUserToStrFor(const AbelType& type) const
{
    if (type.kind != TypeKind::Struct)
        return false;
    const FunctionDeclNode* fn = m_functions.value(QStringLiteral("to_str"), nullptr);
    if (!fn || fn->params.size() != 1 || fn->params.front()->variadic)
        return false;
    const AbelType returnType = typeFromAst(*fn->returnType);
    if (returnType.kind != TypeKind::Str)
        return false;
    const AbelType paramType = typeFromAst(*fn->params.front()->type);
    return isAssignable(paramType, type);
}

AbelType TypeChecker::valueTypeOfVariable(const AbelType& type) const
{
    if (type.isReference() && type.pointee)
        return *type.pointee;
    return type;
}

ExprType TypeChecker::errorExpr(const SourceSpan& span, const QString& message)
{
    error(span, message);
    return {makeType(TypeKind::Unknown), ValueCategory::PRValue, false};
}

void TypeChecker::error(const SourceSpan& span, const QString& message, const QString& code)
{
    Diagnostic d;
    d.severity = Severity::Error;
    d.code = code;
    d.message = message;
    d.primary = span;
    m_diagnostics.push_back(d);
}

} // namespace abel
