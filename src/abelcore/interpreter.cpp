#include "abelcore/interpreter.h"

#include <cmath>

namespace abel {

InterpreterResult Interpreter::run(const ProgramNode& program)
{
    AbelRuntimeContext ctx;
    m_ctx = &ctx;
    m_functions.clear();

    InterpreterResult result;
    if (!collectFunctions(program, ctx)) {
        result.exitCode = 1;
        result.diagnostics = ctx.takeDiagnostics();
        m_ctx = nullptr;
        return result;
    }

    const FunctionDeclNode* main = m_functions.value(QStringLiteral("main"), nullptr);
    if (!main) {
        ctx.error(QStringLiteral("E0504"), QStringLiteral("missing fn int main() or fn void main()"), program.span);
        result.exitCode = 1;
        result.diagnostics = ctx.takeDiagnostics();
        m_ctx = nullptr;
        return result;
    }

    const AbelType mainType = typeFromAst(*main->returnType);
    if (mainType.kind != TypeKind::I32 && mainType.kind != TypeKind::Void) {
        ctx.error(QStringLiteral("E0505"), QStringLiteral("main must return int or void"), main->span);
        result.exitCode = 1;
        result.diagnostics = ctx.takeDiagnostics();
        m_ctx = nullptr;
        return result;
    }

    const ExecResult flow = callFunction(*main, {});
    result.returnValue = flow.value;
    result.diagnostics = ctx.takeDiagnostics();
    if (!result.diagnostics.isEmpty()) {
        result.exitCode = 1;
    } else if (mainType.kind == TypeKind::Void) {
        result.exitCode = 0;
    } else {
        result.exitCode = static_cast<int>(flow.value.asInt());
    }
    m_ctx = nullptr;
    return result;
}

bool Interpreter::collectFunctions(const ProgramNode& program, AbelRuntimeContext& ctx)
{
    bool ok = true;
    for (const auto& decl : program.declarations) {
        if (auto* fn = dynamic_cast<FunctionDeclNode*>(decl.get())) {
            if (m_functions.contains(fn->name)) {
                ctx.error(QStringLiteral("E0506"), QStringLiteral("duplicate function '%1'").arg(fn->name), fn->span);
                ok = false;
                continue;
            }
            m_functions.insert(fn->name, fn);
        }
    }
    return ok;
}

ExecResult Interpreter::callFunction(const FunctionDeclNode& fn, const std::vector<AbelValue>& args)
{
    if (fn.debt || !fn.body) {
        error(QStringLiteral("E0507"), QStringLiteral("function '%1' has no Abel body").arg(fn.name), fn.span);
        return ExecResult::returned(AbelValue::makeUnknown());
    }
    if (args.size() != fn.params.size()) {
        error(QStringLiteral("E0508"),
              QStringLiteral("function '%1' expects %2 argument(s), got %3")
                  .arg(fn.name)
                  .arg(fn.params.size())
                  .arg(args.size()),
              fn.span);
        return ExecResult::returned(AbelValue::makeUnknown());
    }

    m_ctx->pushFrame();
    for (size_t i = 0; i < args.size(); ++i) {
        const ParameterNode& p = *fn.params[i];
        const AbelType target = typeFromAst(*p.type);
        AbelValue converted = convertOrError(args[i], target, p.span);
        m_ctx->defineValueVariable(p.name, converted, false, p.span);
    }
    if (m_ctx->hasError()) {
        m_ctx->popFrame();
        return ExecResult::returned(AbelValue::makeUnknown());
    }

    ExecResult flow = execBlock(*fn.body);
    m_ctx->popFrame();

    const AbelType returnType = typeFromAst(*fn.returnType);
    if (flow.kind == FlowKind::Return) {
        AbelValue converted = convertOrError(flow.value, returnType, fn.span);
        return ExecResult::returned(converted);
    }
    if (flow.kind == FlowKind::Break || flow.kind == FlowKind::Continue) {
        error(QStringLiteral("E0532"), QStringLiteral("break/continue cannot leave function '%1'").arg(fn.name), fn.span);
        return ExecResult::returned(AbelValue::makeUnknown());
    }
    if (returnType.kind == TypeKind::Void)
        return ExecResult::returned(AbelValue::makeVoid());
    error(QStringLiteral("E0509"), QStringLiteral("function '%1' ended without return").arg(fn.name), fn.span);
    return ExecResult::returned(AbelValue::makeUnknown());
}

ExecResult Interpreter::callFunctionExpr(const FunctionDeclNode& fn,
                                         const std::vector<std::unique_ptr<ExprNode>>& args,
                                         const SourceSpan& span)
{
    if (fn.debt || !fn.body) {
        error(QStringLiteral("E0539"), QStringLiteral("function '%1' has no Abel body").arg(fn.name), fn.span);
        return ExecResult::returned(AbelValue::makeUnknown());
    }
    if (args.size() != fn.params.size()) {
        error(QStringLiteral("E0540"),
              QStringLiteral("function '%1' expects %2 argument(s), got %3")
                  .arg(fn.name)
                  .arg(fn.params.size())
                  .arg(args.size()),
              span);
        return ExecResult::returned(AbelValue::makeUnknown());
    }

    m_ctx->pushFrame();
    for (size_t i = 0; i < args.size(); ++i) {
        const ParameterNode& p = *fn.params[i];
        const AbelType target = typeFromAst(*p.type);
        if (target.isReference()) {
            AbelLocation* loc = evalLocation(*args[i]);
            if (!loc)
                continue;
            AbelValue current = loc->read();
            if (!target.pointee || !canAssignValue(*target.pointee, current.type())) {
                error(QStringLiteral("E0541"),
                      QStringLiteral("cannot bind parameter '%1' of type %2 to %3 lvalue")
                          .arg(p.name, target.displayName(), current.type().displayName()),
                      p.span);
                continue;
            }
            m_ctx->defineVariable(p.name, loc, false, true, p.span);
        } else {
            AbelValue converted = convertOrError(evalExpr(*args[i]), target, p.span);
            m_ctx->defineValueVariable(p.name, converted, false, p.span);
        }
    }
    if (m_ctx->hasError()) {
        m_ctx->popFrame();
        return ExecResult::returned(AbelValue::makeUnknown());
    }

    ExecResult flow = execBlock(*fn.body);
    m_ctx->popFrame();

    const AbelType returnType = typeFromAst(*fn.returnType);
    if (flow.kind == FlowKind::Return)
        return ExecResult::returned(convertOrError(flow.value, returnType, fn.span));
    if (flow.kind == FlowKind::Break || flow.kind == FlowKind::Continue) {
        error(QStringLiteral("E0542"), QStringLiteral("break/continue cannot leave function '%1'").arg(fn.name), fn.span);
        return ExecResult::returned(AbelValue::makeUnknown());
    }
    if (returnType.kind == TypeKind::Void)
        return ExecResult::returned(AbelValue::makeVoid());
    error(QStringLiteral("E0543"), QStringLiteral("function '%1' ended without return").arg(fn.name), fn.span);
    return ExecResult::returned(AbelValue::makeUnknown());
}

ExecResult Interpreter::execBlock(const BlockStmtNode& block)
{
    for (const auto& stmt : block.statements) {
        ExecResult r = execStmt(*stmt);
        if (r.kind != FlowKind::Normal)
            return r;
        if (m_ctx->hasError())
            return ExecResult::normal();
    }
    return ExecResult::normal();
}

ExecResult Interpreter::execStmt(const StmtNode& stmt)
{
    if (auto* s = dynamic_cast<const ReturnStmtNode*>(&stmt)) {
        AbelValue value = s->expr ? evalExpr(*s->expr) : AbelValue::makeVoid();
        return ExecResult::returned(value);
    }
    if (auto* s = dynamic_cast<const VarDeclStmtNode*>(&stmt))
        return execVarDecl(*s);
    if (auto* s = dynamic_cast<const ExprStmtNode*>(&stmt)) {
        evalExpr(*s->expr);
        return ExecResult::normal();
    }
    if (auto* s = dynamic_cast<const BlockStmtNode*>(&stmt)) {
        m_ctx->pushFrame();
        ExecResult r = execBlock(*s);
        m_ctx->popFrame();
        return r;
    }
    if (auto* s = dynamic_cast<const IfStmtNode*>(&stmt)) {
        for (const auto& branch : s->branches) {
            bool take = true;
            if (branch.condition) {
                AbelValue cond = evalExpr(*branch.condition);
                if (!requireBool(cond, branch.condition->span, take))
                    return ExecResult::normal();
            }
            if (take)
                return execStmt(*branch.body);
        }
        return ExecResult::normal();
    }
    if (auto* s = dynamic_cast<const WhileStmtNode*>(&stmt)) {
        for (;;) {
            bool cond = false;
            AbelValue value = evalExpr(*s->condition);
            if (!requireBool(value, s->condition->span, cond) || !cond)
                break;
            ExecResult r = execStmt(*s->body);
            if (r.kind == FlowKind::Return)
                return r;
            if (r.kind == FlowKind::Break)
                break;
            if (r.kind == FlowKind::Continue)
                continue;
            if (m_ctx->hasError())
                break;
        }
        return ExecResult::normal();
    }
    if (auto* s = dynamic_cast<const RepeatStmtNode*>(&stmt)) {
        qint64 count = 0;
        AbelValue value = evalExpr(*s->count);
        if (!requireInteger(value, s->count->span, count))
            return ExecResult::normal();
        for (qint64 i = 0; i < count; ++i) {
            ExecResult r = execStmt(*s->body);
            if (r.kind == FlowKind::Return)
                return r;
            if (r.kind == FlowKind::Break)
                break;
            if (r.kind == FlowKind::Continue)
                continue;
            if (m_ctx->hasError())
                break;
        }
        return ExecResult::normal();
    }
    if (dynamic_cast<const BreakStmtNode*>(&stmt))
        return ExecResult::breakFlow();
    if (dynamic_cast<const ContinueStmtNode*>(&stmt))
        return ExecResult::continueFlow();

    error(QStringLiteral("E0510"), QStringLiteral("statement is not implemented in the Stage 3 interpreter"), stmt.span);
    return ExecResult::normal();
}

ExecResult Interpreter::execVarDecl(const VarDeclStmtNode& stmt)
{
    const AbelType type = typeFromAst(*stmt.type);
    if (type.kind == TypeKind::Unknown) {
        error(QStringLiteral("E0511"),
              QStringLiteral("type '%1' is not supported by the current interpreter").arg(stmt.type->displayName()),
              stmt.span);
        return ExecResult::normal();
    }
    if (type.isReference()) {
        if (!stmt.init) {
            error(QStringLiteral("E0533"), QStringLiteral("reference variable '%1' must be initialized").arg(stmt.name), stmt.span);
            return ExecResult::normal();
        }
        AbelLocation* loc = evalLocation(*stmt.init);
        if (!loc)
            return ExecResult::normal();
        const AbelType& referred = *type.pointee;
        AbelValue current = loc->read();
        if (!canAssignValue(referred, current.type())) {
            error(QStringLiteral("E0534"),
                  QStringLiteral("cannot bind %1& to %2 lvalue")
                      .arg(referred.displayName(), current.type().displayName()),
                  stmt.span);
            return ExecResult::normal();
        }
        m_ctx->defineVariable(stmt.name, loc, stmt.isConst || stmt.type->isConst, true, stmt.span);
        return ExecResult::normal();
    }
    AbelValue value = stmt.init ? convertOrError(evalExpr(*stmt.init), type, stmt.span) : defaultValueForType(type);
    m_ctx->defineValueVariable(stmt.name, value, stmt.isConst || stmt.type->isConst, stmt.span);
    return ExecResult::normal();
}

AbelValue Interpreter::evalExpr(const ExprNode& expr)
{
    if (auto* e = dynamic_cast<const LiteralExprNode*>(&expr)) {
        switch (e->kind) {
        case LiteralExprNode::Kind::Int: return AbelValue::makeInt(e->text.toLongLong(), TypeKind::I32);
        case LiteralExprNode::Kind::Float: return AbelValue::makeDouble(e->text.toDouble());
        case LiteralExprNode::Kind::String: return AbelValue::makeString(e->text);
        case LiteralExprNode::Kind::Char: return AbelValue::makeChar(e->text.isEmpty() ? QChar() : e->text[0]);
        case LiteralExprNode::Kind::Bool: return AbelValue::makeBool(e->text == QStringLiteral("true"));
        case LiteralExprNode::Kind::Nullptr:
            return AbelValue::makeNullptr();
        }
    }
    if (auto* e = dynamic_cast<const NameExprNode*>(&expr)) {
        const VariableSlot* slot = m_ctx->lookupVariable(e->name);
        if (!slot) {
            error(QStringLiteral("E0513"), QStringLiteral("unknown variable '%1'").arg(e->name), e->span);
            return AbelValue::makeUnknown();
        }
        return slot->location ? slot->location->read() : AbelValue::makeUnknown();
    }
    if (auto* e = dynamic_cast<const UnaryExprNode*>(&expr))
        return evalUnary(*e);
    if (auto* e = dynamic_cast<const BinaryExprNode*>(&expr))
        return evalBinary(*e);
    if (auto* e = dynamic_cast<const AssignExprNode*>(&expr))
        return evalAssignment(*e);
    if (auto* e = dynamic_cast<const CallExprNode*>(&expr))
        return evalCall(*e);
    if (dynamic_cast<const StaticAccessExprNode*>(&expr)) {
        error(QStringLiteral("E0514"), QStringLiteral("static/backend access is parsed but not executable in Stage 3"), expr.span);
        return AbelValue::makeUnknown();
    }

    error(QStringLiteral("E0515"), QStringLiteral("expression is not implemented in the Stage 3 interpreter"), expr.span);
    return AbelValue::makeUnknown();
}

AbelLocation* Interpreter::evalLocation(const ExprNode& expr)
{
    if (auto* e = dynamic_cast<const NameExprNode*>(&expr)) {
        VariableSlot* slot = m_ctx->lookupVariable(e->name);
        if (!slot || !slot->location) {
            error(QStringLiteral("E0535"), QStringLiteral("unknown variable '%1'").arg(e->name), e->span);
            return nullptr;
        }
        return slot->location;
    }
    if (auto* e = dynamic_cast<const UnaryExprNode*>(&expr); e && e->op == QStringLiteral("*")) {
        AbelValue ptr = evalExpr(*e->expr);
        if (!ptr.type().isPointer()) {
            error(QStringLiteral("E0536"), QStringLiteral("cannot dereference non-pointer value"), e->span);
            return nullptr;
        }
        AbelLocation* loc = ptr.asPointer();
        if (!loc) {
            error(QStringLiteral("E0537"), QStringLiteral("cannot dereference nullptr"), e->span);
            return nullptr;
        }
        return loc;
    }
    error(QStringLiteral("E0538"), QStringLiteral("expression is not an lvalue"), expr.span);
    return nullptr;
}

AbelValue Interpreter::evalBinary(const BinaryExprNode& expr)
{
    if (expr.op == QStringLiteral("&&") || expr.op == QStringLiteral("||")) {
        bool lhs = false;
        if (!requireBool(evalExpr(*expr.lhs), expr.lhs->span, lhs))
            return AbelValue::makeUnknown();
        if (expr.op == QStringLiteral("&&") && !lhs)
            return AbelValue::makeBool(false);
        if (expr.op == QStringLiteral("||") && lhs)
            return AbelValue::makeBool(true);
        bool rhs = false;
        if (!requireBool(evalExpr(*expr.rhs), expr.rhs->span, rhs))
            return AbelValue::makeUnknown();
        return AbelValue::makeBool(rhs);
    }

    AbelValue lhs = evalExpr(*expr.lhs);
    AbelValue rhs = evalExpr(*expr.rhs);
    if (lhs.type().kind == TypeKind::Unknown || rhs.type().kind == TypeKind::Unknown)
        return AbelValue::makeUnknown();

    const QString& op = expr.op;
    if (op == QStringLiteral("+") && lhs.type().kind == TypeKind::Str && rhs.type().kind == TypeKind::Str)
        return AbelValue::makeString(lhs.asString() + rhs.asString());

    if ((op == QStringLiteral("==") || op == QStringLiteral("!=")) && lhs.type().kind == rhs.type().kind) {
        bool eq = false;
        switch (lhs.type().kind) {
        case TypeKind::Bool: eq = lhs.asBool() == rhs.asBool(); break;
        case TypeKind::I32:
        case TypeKind::I64: eq = lhs.asInt() == rhs.asInt(); break;
        case TypeKind::F64: eq = lhs.asDouble() == rhs.asDouble(); break;
        case TypeKind::Char: eq = lhs.asChar() == rhs.asChar(); break;
        case TypeKind::Str: eq = lhs.asString() == rhs.asString(); break;
        case TypeKind::Pointer: eq = lhs.asPointer() == rhs.asPointer(); break;
        case TypeKind::Nullptr: eq = true; break;
        default: break;
        }
        return AbelValue::makeBool(op == QStringLiteral("==") ? eq : !eq);
    }

    if (op == QStringLiteral("==") || op == QStringLiteral("!=")) {
        const bool pointerNull =
            (lhs.type().kind == TypeKind::Pointer && rhs.type().kind == TypeKind::Nullptr)
            || (lhs.type().kind == TypeKind::Nullptr && rhs.type().kind == TypeKind::Pointer);
        if (pointerNull) {
            AbelLocation* ptr = lhs.type().kind == TypeKind::Pointer ? lhs.asPointer() : rhs.asPointer();
            const bool eq = ptr == nullptr;
            return AbelValue::makeBool(op == QStringLiteral("==") ? eq : !eq);
        }
    }

    if (!lhs.type().isNumeric() || !rhs.type().isNumeric()) {
        error(QStringLiteral("E0516"), QStringLiteral("operator '%1' requires numeric operands").arg(op), expr.span);
        return AbelValue::makeUnknown();
    }

    const bool useDouble = lhs.type().kind == TypeKind::F64 || rhs.type().kind == TypeKind::F64;
    if (op == QStringLiteral("==") || op == QStringLiteral("!=")) {
        const bool eq = useDouble ? lhs.asDouble() == rhs.asDouble() : lhs.asInt() == rhs.asInt();
        return AbelValue::makeBool(op == QStringLiteral("==") ? eq : !eq);
    }
    if (useDouble) {
        const double a = lhs.asDouble();
        const double b = rhs.asDouble();
        if (op == QStringLiteral("+")) return AbelValue::makeDouble(a + b);
        if (op == QStringLiteral("-")) return AbelValue::makeDouble(a - b);
        if (op == QStringLiteral("*")) return AbelValue::makeDouble(a * b);
        if (op == QStringLiteral("/")) return AbelValue::makeDouble(a / b);
        if (op == QStringLiteral("**")) return AbelValue::makeDouble(std::pow(a, b));
        if (op == QStringLiteral("<")) return AbelValue::makeBool(a < b);
        if (op == QStringLiteral("<=")) return AbelValue::makeBool(a <= b);
        if (op == QStringLiteral(">")) return AbelValue::makeBool(a > b);
        if (op == QStringLiteral(">=")) return AbelValue::makeBool(a >= b);
        if (op == QStringLiteral("<?")) return AbelValue::makeDouble(a < b ? a : b);
        if (op == QStringLiteral(">?")) return AbelValue::makeDouble(a > b ? a : b);
    } else {
        const qint64 a = lhs.asInt();
        const qint64 b = rhs.asInt();
        const TypeKind resultKind = lhs.type().kind == TypeKind::I64 || rhs.type().kind == TypeKind::I64
            ? TypeKind::I64
            : TypeKind::I32;
        if (op == QStringLiteral("+")) return AbelValue::makeInt(a + b, resultKind);
        if (op == QStringLiteral("-")) return AbelValue::makeInt(a - b, resultKind);
        if (op == QStringLiteral("*")) return AbelValue::makeInt(a * b, resultKind);
        if (op == QStringLiteral("/")) {
            if (b == 0) {
                error(QStringLiteral("E0517"), QStringLiteral("division by zero"), expr.span);
                return AbelValue::makeUnknown();
            }
            return AbelValue::makeInt(a / b, resultKind);
        }
        if (op == QStringLiteral("%") || op == QStringLiteral("%%")) {
            if (b == 0) {
                error(QStringLiteral("E0518"), QStringLiteral("modulo by zero"), expr.span);
                return AbelValue::makeUnknown();
            }
            qint64 r = a % b;
            if (op == QStringLiteral("%%") && r < 0)
                r += b < 0 ? -b : b;
            return AbelValue::makeInt(r, resultKind);
        }
        if (op == QStringLiteral("**")) {
            if (b < 0) {
                error(QStringLiteral("E0519"), QStringLiteral("integer power with negative exponent is not supported"), expr.span);
                return AbelValue::makeUnknown();
            }
            qint64 out = 1;
            for (qint64 i = 0; i < b; ++i)
                out *= a;
            return AbelValue::makeInt(out, resultKind);
        }
        if (op == QStringLiteral("<")) return AbelValue::makeBool(a < b);
        if (op == QStringLiteral("<=")) return AbelValue::makeBool(a <= b);
        if (op == QStringLiteral(">")) return AbelValue::makeBool(a > b);
        if (op == QStringLiteral(">=")) return AbelValue::makeBool(a >= b);
        if (op == QStringLiteral("<?")) return AbelValue::makeInt(a < b ? a : b, resultKind);
        if (op == QStringLiteral(">?")) return AbelValue::makeInt(a > b ? a : b, resultKind);
    }

    error(QStringLiteral("E0520"), QStringLiteral("operator '%1' is not implemented").arg(op), expr.span);
    return AbelValue::makeUnknown();
}

AbelValue Interpreter::evalUnary(const UnaryExprNode& expr)
{
    if (expr.op == QStringLiteral("&")) {
        AbelLocation* loc = evalLocation(*expr.expr);
        if (!loc)
            return AbelValue::makeUnknown();
        AbelValue current = loc->read();
        return AbelValue::makePointer(current.type(), loc);
    }
    if (expr.op == QStringLiteral("*")) {
        AbelLocation* loc = evalLocation(expr);
        return loc ? loc->read() : AbelValue::makeUnknown();
    }
    AbelValue value = evalExpr(*expr.expr);
    if (expr.op == QStringLiteral("!")) {
        bool b = false;
        if (!requireBool(value, expr.span, b))
            return AbelValue::makeUnknown();
        return AbelValue::makeBool(!b);
    }
    if (expr.op == QStringLiteral("+")) {
        if (!value.type().isNumeric()) {
            error(QStringLiteral("E0521"), QStringLiteral("unary + requires numeric operand"), expr.span);
            return AbelValue::makeUnknown();
        }
        return value;
    }
    if (expr.op == QStringLiteral("-")) {
        if (!value.type().isNumeric()) {
            error(QStringLiteral("E0522"), QStringLiteral("unary - requires numeric operand"), expr.span);
            return AbelValue::makeUnknown();
        }
        if (value.type().kind == TypeKind::F64)
            return AbelValue::makeDouble(-value.asDouble());
        return AbelValue::makeInt(-value.asInt(), value.type().kind);
    }
    error(QStringLiteral("E0523"), QStringLiteral("unary operator '%1' is not executable here").arg(expr.op), expr.span);
    return AbelValue::makeUnknown();
}

AbelValue Interpreter::evalCall(const CallExprNode& expr)
{
    auto* name = dynamic_cast<NameExprNode*>(expr.callee.get());
    if (!name) {
        error(QStringLiteral("E0524"), QStringLiteral("only direct function calls are executable in Stage 3"), expr.span);
        return AbelValue::makeUnknown();
    }
    const FunctionDeclNode* fn = m_functions.value(name->name, nullptr);
    if (!fn) {
        error(QStringLiteral("E0525"), QStringLiteral("unknown function '%1'").arg(name->name), expr.span);
        return AbelValue::makeUnknown();
    }
    return callFunctionExpr(*fn, expr.args, expr.span).value;
}

AbelValue Interpreter::evalAssignment(const AssignExprNode& expr)
{
    auto* name = dynamic_cast<NameExprNode*>(expr.lhs.get());
    if (name) {
        VariableSlot* slot = m_ctx->lookupVariable(name->name);
        if (!slot) {
            error(QStringLiteral("E0527"), QStringLiteral("unknown variable '%1'").arg(name->name), expr.span);
            return AbelValue::makeUnknown();
        }
        AbelValue current = slot->location ? slot->location->read() : AbelValue::makeUnknown();
        AbelValue rhs = convertOrError(evalExpr(*expr.rhs), current.type(), expr.span);
        m_ctx->assignVariable(name->name, rhs, expr.span);
        return rhs;
    }

    AbelLocation* lhs = evalLocation(*expr.lhs);
    if (!lhs) {
        error(QStringLiteral("E0526"), QStringLiteral("left side of assignment is not an lvalue"), expr.span);
        return AbelValue::makeUnknown();
    }
    AbelValue current = lhs->read();
    AbelValue rhs = convertOrError(evalExpr(*expr.rhs), current.type(), expr.span);
    lhs->write(rhs);
    return rhs;
}

bool Interpreter::requireBool(const AbelValue& value, const SourceSpan& span, bool& out)
{
    if (value.type().kind != TypeKind::Bool) {
        error(QStringLiteral("E0528"), QStringLiteral("condition must be bool, got %1").arg(value.type().displayName()), span);
        return false;
    }
    out = value.asBool();
    return true;
}

bool Interpreter::requireInteger(const AbelValue& value, const SourceSpan& span, qint64& out)
{
    if (!value.type().isInteger()) {
        error(QStringLiteral("E0529"), QStringLiteral("repeat count must be integer, got %1").arg(value.type().displayName()), span);
        return false;
    }
    out = value.asInt();
    return true;
}

AbelValue Interpreter::convertOrError(const AbelValue& value, const AbelType& target, const SourceSpan& span)
{
    if (target.kind == TypeKind::Unknown) {
        error(QStringLiteral("E0530"), QStringLiteral("unsupported target type '%1'").arg(target.displayName()), span);
        return AbelValue::makeUnknown();
    }
    if (!canAssignValue(target, value.type())) {
        error(QStringLiteral("E0531"),
              QStringLiteral("cannot assign %1 to %2").arg(value.type().displayName(), target.displayName()),
              span);
        return AbelValue::makeUnknown();
    }
    return convertValue(value, target);
}

void Interpreter::error(const QString& code, const QString& message, const SourceSpan& span)
{
    m_ctx->error(code, message, span);
}

} // namespace abel
