#include "abelcore/typechecker.h"

namespace abel {

TypeCheckResult TypeChecker::check(const ProgramNode& program)
{
    m_functions.clear();
    m_scopes.clear();
    m_diagnostics.clear();
    m_currentReturnType = makeType(TypeKind::Void);
    m_currentStruct.clear();
    m_loopDepth = 0;

    collectStructs(program);
    collectFunctions(program);

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
        if (!isAssignable(m_currentReturnType, value.type))
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
                if (cond.type.kind != TypeKind::Bool)
                    error(branch.condition->span, QStringLiteral("if condition must be bool, got %1").arg(cond.type.displayName()));
            }
            checkBlock(*branch.body, true);
        }
        return;
    }
    if (auto* s = dynamic_cast<const WhileStmtNode*>(&stmt)) {
        ExprType cond = checkExpr(*s->condition);
        if (cond.type.kind != TypeKind::Bool)
            error(s->condition->span, QStringLiteral("while condition must be bool, got %1").arg(cond.type.displayName()));
        ++m_loopDepth;
        checkBlock(*s->body, true);
        --m_loopDepth;
        return;
    }
    if (auto* s = dynamic_cast<const RepeatStmtNode*>(&stmt)) {
        ExprType count = checkExpr(*s->count);
        if (!count.type.isInteger())
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
        if (init.category != ValueCategory::LValue)
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
        if (cond.type.kind != TypeKind::Bool)
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
    if (auto* e = dynamic_cast<const AssignExprNode*>(&expr)) return checkAssignment(*e);
    if (auto* e = dynamic_cast<const CallExprNode*>(&expr)) return checkCall(*e);
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
    const VariableInfo* var = lookupVariable(expr.name);
    if (!var)
        return errorExpr(expr.span, QStringLiteral("unknown variable '%1'").arg(expr.name));
    return {valueTypeOfVariable(var->type), ValueCategory::LValue, !var->isConst};
}

ExprType TypeChecker::checkUnary(const UnaryExprNode& expr)
{
    if (expr.op == QStringLiteral("&")) {
        ExprType inner = checkExpr(*expr.expr);
        if (inner.category != ValueCategory::LValue)
            return errorExpr(expr.span, QStringLiteral("address-of requires lvalue"));
        return {makePointerType(inner.type), ValueCategory::PRValue, false};
    }
    if (expr.op == QStringLiteral("*")) {
        ExprType inner = checkExpr(*expr.expr);
        if (!inner.type.isPointer() || !inner.type.pointee)
            return errorExpr(expr.span, QStringLiteral("dereference requires pointer"));
        return {*inner.type.pointee, ValueCategory::LValue, true};
    }
    ExprType inner = checkExpr(*expr.expr);
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
    ExprType lhs = checkExpr(*expr.lhs);
    ExprType rhs = checkExpr(*expr.rhs);
    const QString& op = expr.op;

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

ExprType TypeChecker::checkAssignment(const AssignExprNode& expr)
{
    ExprType lhs = checkExpr(*expr.lhs);
    ExprType rhs = checkExpr(*expr.rhs);
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
    if (auto* field = dynamic_cast<FieldAccessExprNode*>(expr.callee.get())) {
        ExprType receiver = checkExpr(*field->base);
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
                if (!isAssignable(paramType, arg.type))
                    error(expr.args[i]->span, QStringLiteral("cannot pass %1 to method parameter %2").arg(arg.type.displayName(), paramType.displayName()));
            }
            return {typeFromAst(*method->returnType), method->returnType->isReference ? ValueCategory::LValue : ValueCategory::PRValue, false};
        }
        return checkBuiltinMethodCall(*field, expr.args);
    }

    auto* name = dynamic_cast<NameExprNode*>(expr.callee.get());
    if (!name)
        return errorExpr(expr.span, QStringLiteral("only direct function and builtin method calls are supported"));

    if (const StructInfo* info = m_structs.contains(name->name) ? &m_structs[name->name] : nullptr) {
        const size_t argc = expr.args.size();
        if (!info->constructor) {
            if (argc != info->fields.size())
                return errorExpr(expr.span, QStringLiteral("constructor '%1' expects %2 argument(s)").arg(name->name).arg(info->fields.size()));
            for (size_t i = 0; i < argc; ++i) {
                const QString& fieldName = info->decl->fields[i]->name;
                ExprType arg = checkExpr(*expr.args[i]);
                if (!isAssignable(info->fields.value(fieldName).type, arg.type))
                    error(expr.args[i]->span, QStringLiteral("cannot initialize field '%1'").arg(fieldName));
            }
        } else {
            if (argc != info->constructor->params.size())
                return errorExpr(expr.span, QStringLiteral("constructor '%1' called with wrong argument count").arg(name->name));
            for (size_t i = 0; i < argc; ++i) {
                AbelType paramType = typeFromAst(*info->constructor->params[i]->type);
                ExprType arg = checkExpr(*expr.args[i]);
                if (!isAssignable(paramType, arg.type))
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
        if (name->name == QStringLiteral("to_str") && argc != 1)
            return errorExpr(expr.span, QStringLiteral("to_str expects one argument"));
        for (const auto& arg : expr.args)
            checkExpr(*arg);
        if (name->name == QStringLiteral("to_str") || name->name == QStringLiteral("build_string"))
            return {makeType(TypeKind::Str), ValueCategory::PRValue, false};
        if (name->name == QStringLiteral("print") || name->name == QStringLiteral("println"))
            return {makeType(TypeKind::Void), ValueCategory::PRValue, false};
    }

    return errorExpr(expr.span, QStringLiteral("unknown function '%1'").arg(name->name));
}

ExprType TypeChecker::checkBuiltinMethodCall(const FieldAccessExprNode& callee, const std::vector<std::unique_ptr<ExprNode>>& args)
{
    ExprType receiver = checkExpr(*callee.base);
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
        if (receiver.category != ValueCategory::LValue || !receiver.isMutable)
            return errorExpr(callee.span, QStringLiteral("vector.push requires mutable vector lvalue"));
        ExprType arg = checkExpr(*args[0]);
        if (!isAssignable(element, arg.type))
            error(args[0]->span, QStringLiteral("cannot push %1 into vector<%2>").arg(arg.type.displayName(), element.displayName()));
        return {makeType(TypeKind::Void), ValueCategory::PRValue, false};
    }
    if (callee.field == QStringLiteral("pop") || callee.field == QStringLiteral("clear")) {
        if (!args.empty())
            return errorExpr(callee.span, QStringLiteral("vector.%1 expects no arguments").arg(callee.field));
        if (receiver.category != ValueCategory::LValue || !receiver.isMutable)
            return errorExpr(callee.span, QStringLiteral("vector.%1 requires mutable vector lvalue").arg(callee.field));
        return callee.field == QStringLiteral("pop")
            ? ExprType{element, ValueCategory::PRValue, false}
            : ExprType{makeType(TypeKind::Void), ValueCategory::PRValue, false};
    }
    if (callee.field == QStringLiteral("reserve") || callee.field == QStringLiteral("resize")) {
        if (args.size() != 1)
            return errorExpr(callee.span, QStringLiteral("vector.%1 expects one argument").arg(callee.field));
        if (receiver.category != ValueCategory::LValue || !receiver.isMutable)
            return errorExpr(callee.span, QStringLiteral("vector.%1 requires mutable vector lvalue").arg(callee.field));
        ExprType count = checkExpr(*args[0]);
        if (!count.type.isInteger())
            error(args[0]->span, QStringLiteral("vector.%1 size must be integer").arg(callee.field));
        return {makeType(TypeKind::Void), ValueCategory::PRValue, false};
    }
    if (callee.field == QStringLiteral("front") || callee.field == QStringLiteral("back")) {
        if (!args.empty())
            return errorExpr(callee.span, QStringLiteral("vector.%1 expects no arguments").arg(callee.field));
        if (receiver.category != ValueCategory::LValue)
            return errorExpr(callee.span, QStringLiteral("vector.%1 requires vector lvalue").arg(callee.field));
        return {element, ValueCategory::LValue, receiver.isMutable};
    }
    return errorExpr(callee.span, QStringLiteral("unsupported builtin method '%1'").arg(callee.field));
}

ExprType TypeChecker::checkIndex(const IndexExprNode& expr)
{
    ExprType base = checkExpr(*expr.base);
    ExprType index = checkExpr(*expr.index);
    if (base.type.kind != TypeKind::Vector || !base.type.pointee)
        return errorExpr(expr.span, QStringLiteral("indexing requires vector"));
    if (!index.type.isInteger())
        error(expr.index->span, QStringLiteral("vector index must be integer"));
    return {*base.type.pointee, ValueCategory::LValue, base.isMutable};
}

ExprType TypeChecker::checkInitListAgainst(const InitListExprNode& init, const AbelType& target)
{
    if (target.kind != TypeKind::Vector || !target.pointee)
        return errorExpr(init.span, QStringLiteral("initializer list requires vector target"));
    for (const auto& value : init.values) {
        ExprType element = checkExpr(*value);
        if (!isAssignable(*target.pointee, element.type))
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
    AbelType objectType = base.type;
    bool mutableBase = base.isMutable;
    if (expr.pointer) {
        if (!objectType.isPointer() || !objectType.pointee)
            return errorExpr(expr.span, QStringLiteral("operator -> requires pointer receiver"));
        objectType = *objectType.pointee;
    }
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
    if ((type.kind == TypeKind::Pointer || type.kind == TypeKind::Reference || type.kind == TypeKind::Vector) && type.pointee)
        return isSupportedType(*type.pointee);
    return true;
}

bool TypeChecker::isAssignable(const AbelType& target, const AbelType& source) const
{
    return canAssignValue(target, source);
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
