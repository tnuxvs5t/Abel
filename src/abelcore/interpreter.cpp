#include "abelcore/interpreter.h"

#include <QSet>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <functional>

namespace abel {

namespace {

QString functionFrameSymbol(const FunctionDeclNode& fn)
{
    return QStringLiteral("fn %1").arg(fn.name);
}

QString methodFrameSymbol(const FunctionDeclNode& fn)
{
    return QStringLiteral("method %1").arg(fn.name);
}

QString lambdaFrameSymbol()
{
    return QStringLiteral("lambda");
}

QString constructorFrameSymbol(const QString& name)
{
    return QStringLiteral("init %1").arg(name);
}

QString backendFrameSymbol(const QString& backendId, const QString& symbol)
{
    return QStringLiteral("backend %1::%2").arg(backendId, symbol);
}

bool isPipeHoleExpr(const ExprNode& expr)
{
    auto* name = dynamic_cast<const NameExprNode*>(&expr);
    return name && name->name == QStringLiteral("_");
}

bool hasPipeHole(const std::vector<std::unique_ptr<ExprNode>>& args)
{
    for (const auto& arg : args) {
        if (isPipeHoleExpr(*arg))
            return true;
    }
    return false;
}

bool isCompoundAssignmentOperator(const QString& op)
{
    return op == QStringLiteral("+=")
        || op == QStringLiteral("-=")
        || op == QStringLiteral("*=")
        || op == QStringLiteral("/=")
        || op == QStringLiteral("%=")
        || op == QStringLiteral("%%=")
        || op == QStringLiteral("**=")
        || op == QStringLiteral("<?=")
        || op == QStringLiteral(">?=");
}

QString compoundAssignmentBaseOperator(const QString& op)
{
    if (op == QStringLiteral("+=")) return QStringLiteral("+");
    if (op == QStringLiteral("-=")) return QStringLiteral("-");
    if (op == QStringLiteral("*=")) return QStringLiteral("*");
    if (op == QStringLiteral("/=")) return QStringLiteral("/");
    if (op == QStringLiteral("%=")) return QStringLiteral("%");
    if (op == QStringLiteral("%%=")) return QStringLiteral("%%");
    if (op == QStringLiteral("**=")) return QStringLiteral("**");
    if (op == QStringLiteral("<?=")) return QStringLiteral("<?");
    if (op == QStringLiteral(">?=")) return QStringLiteral(">?");
    return {};
}

int countPipeHolesInBlock(const BlockStmtNode& block);
int countPipeHolesInStmt(const StmtNode& stmt);

int countPipeHoles(const ExprNode& expr)
{
    if (isPipeHoleExpr(expr))
        return 1;
    if (auto* unary = dynamic_cast<const UnaryExprNode*>(&expr))
        return countPipeHoles(*unary->expr);
    if (auto* binary = dynamic_cast<const BinaryExprNode*>(&expr))
        return countPipeHoles(*binary->lhs) + countPipeHoles(*binary->rhs);
    if (auto* assign = dynamic_cast<const AssignExprNode*>(&expr))
        return countPipeHoles(*assign->lhs) + countPipeHoles(*assign->rhs);
    if (auto* cast = dynamic_cast<const CastExprNode*>(&expr))
        return countPipeHoles(*cast->expr);
    if (auto* call = dynamic_cast<const CallExprNode*>(&expr)) {
        int count = countPipeHoles(*call->callee);
        for (const auto& arg : call->args)
            count += countPipeHoles(*arg);
        return count;
    }
    if (auto* index = dynamic_cast<const IndexExprNode*>(&expr))
        return countPipeHoles(*index->base) + countPipeHoles(*index->index);
    if (auto* field = dynamic_cast<const FieldAccessExprNode*>(&expr))
        return countPipeHoles(*field->base);
    if (auto* access = dynamic_cast<const StaticAccessExprNode*>(&expr))
        return countPipeHoles(*access->base);
    if (auto* init = dynamic_cast<const InitListExprNode*>(&expr)) {
        int count = 0;
        for (const auto& value : init->values)
            count += countPipeHoles(*value);
        return count;
    }
    if (auto* tuple = dynamic_cast<const AnyTupleLiteralExprNode*>(&expr)) {
        int count = 0;
        for (const auto& value : tuple->values)
            count += countPipeHoles(*value);
        return count;
    }
    if (auto* map = dynamic_cast<const StrMapLiteralExprNode*>(&expr)) {
        int count = 0;
        for (const auto& entry : map->entries)
            count += countPipeHoles(*entry.value);
        return count;
    }
    if (auto* doExpr = dynamic_cast<const DoExprNode*>(&expr))
        return doExpr->ownedBody ? countPipeHolesInBlock(*doExpr->ownedBody) : 0;
    return 0;
}

int countPipeHolesInStmt(const StmtNode& stmt)
{
    if (auto* block = dynamic_cast<const BlockStmtNode*>(&stmt))
        return countPipeHolesInBlock(*block);
    if (auto* expr = dynamic_cast<const ExprStmtNode*>(&stmt))
        return expr->expr ? countPipeHoles(*expr->expr) : 0;
    if (auto* ret = dynamic_cast<const ReturnStmtNode*>(&stmt))
        return ret->expr ? countPipeHoles(*ret->expr) : 0;
    if (auto* var = dynamic_cast<const VarDeclStmtNode*>(&stmt))
        return var->init ? countPipeHoles(*var->init) : 0;
    if (auto* tuple = dynamic_cast<const TupleCastStmtNode*>(&stmt))
        return tuple->rhs ? countPipeHoles(*tuple->rhs) : 0;
    if (auto* ifStmt = dynamic_cast<const IfStmtNode*>(&stmt)) {
        int count = 0;
        for (const auto& branch : ifStmt->branches) {
            if (branch.condition)
                count += countPipeHoles(*branch.condition);
            if (branch.body)
                count += countPipeHolesInBlock(*branch.body);
        }
        return count;
    }
    if (auto* whileStmt = dynamic_cast<const WhileStmtNode*>(&stmt))
        return (whileStmt->condition ? countPipeHoles(*whileStmt->condition) : 0)
            + (whileStmt->body ? countPipeHolesInBlock(*whileStmt->body) : 0);
    if (auto* repeat = dynamic_cast<const RepeatStmtNode*>(&stmt))
        return (repeat->count ? countPipeHoles(*repeat->count) : 0)
            + (repeat->body ? countPipeHolesInBlock(*repeat->body) : 0);
    if (auto* forStmt = dynamic_cast<const ForStmtNode*>(&stmt)) {
        int count = 0;
        if (forStmt->init)
            count += countPipeHolesInStmt(*forStmt->init);
        if (forStmt->condition)
            count += countPipeHoles(*forStmt->condition);
        if (forStmt->step)
            count += countPipeHoles(*forStmt->step);
        if (forStmt->body)
            count += countPipeHolesInBlock(*forStmt->body);
        return count;
    }
    if (auto* rangeFor = dynamic_cast<const RangeForStmtNode*>(&stmt))
        return (rangeFor->range ? countPipeHoles(*rangeFor->range) : 0)
            + (rangeFor->body ? countPipeHolesInBlock(*rangeFor->body) : 0);
    return 0;
}

int countPipeHolesInBlock(const BlockStmtNode& block)
{
    int count = 0;
    for (const auto& stmt : block.statements) {
        if (stmt)
            count += countPipeHolesInStmt(*stmt);
    }
    return count;
}

bool isPipeHoleReceiverExpr(const ExprNode& expr)
{
    if (isPipeHoleExpr(expr))
        return true;
    if (auto* field = dynamic_cast<const FieldAccessExprNode*>(&expr))
        return isPipeHoleReceiverExpr(*field->base);
    if (auto* call = dynamic_cast<const CallExprNode*>(&expr))
        return isPipeHoleReceiverExpr(*call->callee);
    if (auto* index = dynamic_cast<const IndexExprNode*>(&expr))
        return isPipeHoleReceiverExpr(*index->base);
    return false;
}

QString callArgName(const CallExprNode& call, size_t index)
{
    return index < call.argNames.size() ? call.argNames[index] : QString();
}

bool callArgSpread(const CallExprNode& call, size_t index)
{
    return index < call.argSpreads.size() && call.argSpreads[index];
}

std::unique_ptr<TypeNode> cloneTypeNode(const TypeNode& node)
{
    auto out = std::make_unique<TypeNode>();
    out->span = node.span;
    out->name = node.name;
    out->isConst = node.isConst;
    out->pointerDepth = node.pointerDepth;
    out->isReference = node.isReference;
    if (node.elementType)
        out->elementType = cloneTypeNode(*node.elementType);
    out->functionParamTypes.reserve(node.functionParamTypes.size());
    for (const auto& param : node.functionParamTypes)
        out->functionParamTypes.push_back(cloneTypeNode(*param));
    return out;
}

std::unique_ptr<ExprNode> cloneCallableExprNode(const ExprNode& node)
{
    if (auto* name = dynamic_cast<const NameExprNode*>(&node)) {
        auto out = std::make_unique<NameExprNode>();
        out->span = name->span;
        out->name = name->name;
        return out;
    }
    if (auto* access = dynamic_cast<const StaticAccessExprNode*>(&node)) {
        auto out = std::make_unique<StaticAccessExprNode>();
        out->span = access->span;
        out->member = access->member;
        out->memberSpan = access->memberSpan;
        out->base = cloneCallableExprNode(*access->base);
        return out;
    }
    auto out = std::make_unique<NameExprNode>();
    out->span = node.span;
    return out;
}

bool callHasNamedArgs(const CallExprNode& call)
{
    for (size_t i = 0; i < call.args.size(); ++i) {
        if (!callArgName(call, i).isEmpty())
            return true;
    }
    return false;
}

bool callHasSpreadArgs(const CallExprNode& call)
{
    for (size_t i = 0; i < call.args.size(); ++i) {
        if (callArgSpread(call, i))
            return true;
    }
    return false;
}

bool callHasStructuredArgs(const CallExprNode& call)
{
    return callHasNamedArgs(call) || callHasSpreadArgs(call);
}

bool isVectorAnyType(const AbelType& type)
{
    return type.kind == TypeKind::Vector && type.pointee && type.pointee->kind == TypeKind::Any;
}

QString backendSignatureText(const AbelType& returnType, const std::vector<AbelType>& params, bool variadic)
{
    QStringList parts;
    for (size_t i = 0; i < params.size(); ++i) {
        QString text = params[i].displayName();
        if (variadic && i + 1 == params.size())
            text += QStringLiteral("...");
        parts.push_back(text);
    }
    return returnType.displayName() + QStringLiteral("(") + parts.join(QStringLiteral(", ")) + QStringLiteral(")");
}

QString backendSignatureText(const BackendFunctionDesc& desc)
{
    return backendSignatureText(desc.returnType, desc.params, desc.variadic);
}

QString declarationQualifiedName(const DeclNode& decl, const QString& name)
{
    QStringList parts;
    if (!decl.packageName.isEmpty())
        parts.push_back(decl.packageName);
    if (!decl.moduleName.isEmpty())
        parts.push_back(decl.moduleName);
    parts.push_back(name);
    return parts.join(QStringLiteral("::"));
}

QString structTypeName(const StructDeclNode& decl)
{
    return declarationQualifiedName(decl, decl.name);
}

AbelType applyTypeDecorations(AbelType base, const TypeNode& node)
{
    if (node.isConst)
        base = makeConstType(base);
    for (int i = 0; i < node.pointerDepth; ++i)
        base = makePointerType(base);
    if (node.isReference)
        base = makeReferenceType(base);
    return base;
}

bool sameDeclNamespace(const DeclNode& lhs, const DeclNode& rhs)
{
    return lhs.packageName == rhs.packageName
        && lhs.moduleName == rhs.moduleName;
}

QString staticAccessName(const ExprNode& expr)
{
    if (auto* name = dynamic_cast<const NameExprNode*>(&expr))
        return name->name;
    if (auto* field = dynamic_cast<const FieldAccessExprNode*>(&expr)) {
        if (field->pointer)
            return {};
        const QString base = staticAccessName(*field->base);
        if (!base.isEmpty())
            return base + QStringLiteral(".") + field->field;
    }
    if (auto* access = dynamic_cast<const StaticAccessExprNode*>(&expr)) {
        const QString base = staticAccessName(*access->base);
        if (!base.isEmpty())
            return base + QStringLiteral("::") + access->member;
    }
    return {};
}

QString staticAccessModuleName(const StaticAccessExprNode& access)
{
    return staticAccessName(*access.base).replace(QStringLiteral("::"), QStringLiteral("."));
}

std::optional<QPair<QString, QString>> splitQualifiedSymbol(QString name)
{
    const int sep = name.lastIndexOf(QStringLiteral("::"));
    if (sep < 0)
        return std::nullopt;
    QString moduleName = name.left(sep);
    moduleName.replace(QStringLiteral("::"), QStringLiteral("."));
    const QString symbolName = name.mid(sep + 2);
    if (moduleName.isEmpty() || symbolName.isEmpty())
        return std::nullopt;
    return QPair<QString, QString>{moduleName, symbolName};
}

bool exprCanHaveRuntimeLocation(const ExprNode& expr)
{
    if (dynamic_cast<const NameExprNode*>(&expr))
        return true;
    if (dynamic_cast<const FieldAccessExprNode*>(&expr))
        return true;
    if (dynamic_cast<const IndexExprNode*>(&expr))
        return true;
    if (auto* unary = dynamic_cast<const UnaryExprNode*>(&expr))
        return unary->op == QStringLiteral("*");
    if (auto* call = dynamic_cast<const CallExprNode*>(&expr)) {
        auto* field = dynamic_cast<const FieldAccessExprNode*>(call->callee.get());
        return field && (field->field == QStringLiteral("front") || field->field == QStringLiteral("back"));
    }
    return false;
}

AbelLocation* readonlyAlias(AbelRuntimeContext& ctx, AbelLocation* location, bool isReadOnly)
{
    if (!location || !isReadOnly)
        return location;
    return ctx.createAliasLocation(location, true);
}

bool vectorElementReadOnly(const AbelValue& vectorValue, bool receiverReadOnly)
{
    return receiverReadOnly
        || vectorValue.type().isConst
        || (vectorValue.type().pointee && vectorValue.type().pointee->isConst);
}

AbelValue unboxAnyValue(const AbelValue& value)
{
    return unboxAny(value);
}

TypeKind integerTypeForWidth(int width, bool unsignedResult)
{
    if (width <= 8)
        return unsignedResult ? TypeKind::U8 : TypeKind::I8;
    if (width <= 16)
        return unsignedResult ? TypeKind::U16 : TypeKind::I16;
    if (width <= 32)
        return unsignedResult ? TypeKind::U32 : TypeKind::I32;
    return unsignedResult ? TypeKind::U64 : TypeKind::I64;
}

TypeKind numericBinaryResultKind(const AbelType& lhs, const AbelType& rhs)
{
    const int width = std::max({32, lhs.integerBitWidth(), rhs.integerBitWidth()});
    const bool unsignedResult = lhs.isUnsignedInteger() || rhs.isUnsignedInteger();
    return integerTypeForWidth(width, unsignedResult);
}

bool isBuiltinEqualityComparable(const AbelType& lhs, const AbelType& rhs)
{
    if (lhs.isNumeric() && rhs.isNumeric())
        return true;
    if ((lhs.isPointer() && rhs.kind == TypeKind::Nullptr)
        || (lhs.kind == TypeKind::Nullptr && rhs.isPointer()))
        return true;
    if (lhs.kind != rhs.kind)
        return false;
    switch (lhs.kind) {
    case TypeKind::Bool:
    case TypeKind::Char:
    case TypeKind::Str:
    case TypeKind::Pointer:
        return lhs == rhs;
    case TypeKind::Nullptr:
        return true;
    default:
        return false;
    }
}

class DeclContextGuard {
public:
    DeclContextGuard(QString& currentPackage,
                     QString& currentModule,
                     QList<QString>& currentImports,
                     QHash<QString, QString>& currentImportAliases,
                     const DeclNode& decl)
        : m_currentPackage(currentPackage)
        , m_currentModule(currentModule)
        , m_currentImports(currentImports)
        , m_currentImportAliases(currentImportAliases)
        , m_previousPackage(currentPackage)
        , m_previousModule(currentModule)
        , m_previousImports(currentImports)
        , m_previousImportAliases(currentImportAliases)
    {
        m_currentPackage = decl.packageName;
        m_currentModule = decl.moduleName;
        m_currentImports = decl.importedModules;
        m_currentImportAliases = decl.importedModuleAliases;
    }

    DeclContextGuard(QString& currentPackage,
                     QString& currentModule,
                     QList<QString>& currentImports,
                     QHash<QString, QString>& currentImportAliases,
                     QString packageName,
                     QString moduleName,
                     QList<QString> importedModules,
                     QHash<QString, QString> importedModuleAliases)
        : m_currentPackage(currentPackage)
        , m_currentModule(currentModule)
        , m_currentImports(currentImports)
        , m_currentImportAliases(currentImportAliases)
        , m_previousPackage(currentPackage)
        , m_previousModule(currentModule)
        , m_previousImports(currentImports)
        , m_previousImportAliases(currentImportAliases)
    {
        m_currentPackage = std::move(packageName);
        m_currentModule = std::move(moduleName);
        m_currentImports = std::move(importedModules);
        m_currentImportAliases = std::move(importedModuleAliases);
    }

    ~DeclContextGuard()
    {
        m_currentPackage = m_previousPackage;
        m_currentModule = m_previousModule;
        m_currentImports = m_previousImports;
        m_currentImportAliases = m_previousImportAliases;
    }

private:
    QString& m_currentPackage;
    QString& m_currentModule;
    QList<QString>& m_currentImports;
    QHash<QString, QString>& m_currentImportAliases;
    QString m_previousPackage;
    QString m_previousModule;
    QList<QString> m_previousImports;
    QHash<QString, QString> m_previousImportAliases;
};

class CurrentStructGuard {
public:
    CurrentStructGuard(QString& currentStruct, QString structName)
        : m_currentStruct(currentStruct)
        , m_previousStruct(currentStruct)
    {
        m_currentStruct = std::move(structName);
    }

    ~CurrentStructGuard()
    {
        m_currentStruct = m_previousStruct;
    }

private:
    QString& m_currentStruct;
    QString m_previousStruct;
};

bool isSourceLocationBuiltinName(const QString& name)
{
    return name == QStringLiteral("__FILE__")
        || name == QStringLiteral("__LINE__")
        || name == QStringLiteral("__COLUMN__");
}

AbelValue evalSourceLocationBuiltin(const QString& name, const SourceSpan& span)
{
    if (name == QStringLiteral("__FILE__"))
        return AbelValue::makeString(span.file);
    if (name == QStringLiteral("__LINE__"))
        return AbelValue::makeInt(span.startLine, TypeKind::I32);
    return AbelValue::makeInt(span.startColumn, TypeKind::I32);
}

} // namespace

InterpreterResult Interpreter::run(const ProgramNode& program)
{
    return run(program, nullptr);
}

void Interpreter::beginRun(AbelRuntimeContext& ctx, BackendRegistry* backendRegistry)
{
    m_ctx = &ctx;
    m_functions.clear();
    m_structs.clear();
    m_backends.clear();
    m_enums.clear();
    m_typeAliases.clear();
    m_resolvingTypeAliases.clear();
    m_currentPackage.clear();
    m_currentModule.clear();
    m_currentStruct.clear();
    m_currentImports.clear();
    m_currentImportAliases.clear();
    m_backendRegistry = BackendRegistry();
    m_activeBackendRegistry = backendRegistry ? backendRegistry : &m_backendRegistry;
    m_hasPipeHoleArg = false;
    m_pipeHoleArg = PreparedCallArg{};
    m_pipeHoleTempLocation = nullptr;
}

void Interpreter::endRun()
{
    m_ctx = nullptr;
    m_activeBackendRegistry = nullptr;
    m_hasPipeHoleArg = false;
    m_pipeHoleTempLocation = nullptr;
}

bool Interpreter::collectProgram(const ProgramNode& program, AbelRuntimeContext& ctx, InterpreterResult& result)
{
    if (!collectEnums(program, ctx)) {
        result.exitCode = 1;
        result.diagnostics = ctx.takeDiagnostics();
        return false;
    }
    if (!collectTypeAliases(program, ctx)) {
        result.exitCode = 1;
        result.diagnostics = ctx.takeDiagnostics();
        return false;
    }
    if (!collectStructs(program, ctx)) {
        result.exitCode = 1;
        result.diagnostics = ctx.takeDiagnostics();
        return false;
    }
    if (!collectFunctions(program, ctx)) {
        result.exitCode = 1;
        result.diagnostics = ctx.takeDiagnostics();
        return false;
    }
    if (!collectBackends(program, ctx)) {
        result.exitCode = 1;
        result.diagnostics = ctx.takeDiagnostics();
        return false;
    }
    return true;
}

bool Interpreter::validateTestFixture(const FunctionDeclNode* fn, const QString& name, AbelRuntimeContext& ctx)
{
    if (!fn)
        return true;
    bool ok = true;
    const AbelType returnType = typeFromAstForDecl(*fn->returnType, *fn);
    if (returnType.kind != TypeKind::Void || !fn->params.empty()) {
        ctx.error(QStringLiteral("E0506"),
                  QStringLiteral("test fixture '%1' must be declared as fn void %1()").arg(name),
                  fn->span);
        ok = false;
    }
    if (fn->debt || !fn->body) {
        ctx.error(QStringLiteral("E0507"),
                  QStringLiteral("test fixture '%1' must have an Abel body").arg(name),
                  fn->span);
        ok = false;
    }
    return ok;
}

InterpreterResult Interpreter::run(const ProgramNode& program, BackendRegistry* backendRegistry)
{
    AbelRuntimeContext ctx;
    beginRun(ctx, backendRegistry);

    InterpreterResult result;
    if (!collectProgram(program, ctx, result)) {
        endRun();
        return result;
    }

    const FunctionDeclNode* main = findRootFunction(QStringLiteral("main"));
    if (!main) {
        ctx.error(QStringLiteral("E0504"), QStringLiteral("missing fn int main() or fn void main()"), program.span);
        result.exitCode = 1;
        result.diagnostics = ctx.takeDiagnostics();
        endRun();
        return result;
    }

    const AbelType mainType = typeFromAstForDecl(*main->returnType, *main);
    if (mainType.kind != TypeKind::I32 && mainType.kind != TypeKind::Void) {
        ctx.error(QStringLiteral("E0505"), QStringLiteral("main must return int or void"), main->span);
        result.exitCode = 1;
        result.diagnostics = ctx.takeDiagnostics();
        endRun();
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
    endRun();
    return result;
}

InterpreterResult Interpreter::runTest(const ProgramNode& program)
{
    return runTest(program, nullptr);
}

InterpreterResult Interpreter::runTest(const ProgramNode& program, BackendRegistry* backendRegistry)
{
    AbelRuntimeContext ctx;
    beginRun(ctx, backendRegistry);

    InterpreterResult result;
    if (!collectProgram(program, ctx, result)) {
        endRun();
        return result;
    }

    const FunctionDeclNode* main = findRootFunction(QStringLiteral("main"));
    if (!main) {
        ctx.error(QStringLiteral("E0504"), QStringLiteral("missing fn int main() or fn void main()"), program.span);
        result.exitCode = 1;
        result.diagnostics = ctx.takeDiagnostics();
        endRun();
        return result;
    }

    const AbelType mainType = typeFromAstForDecl(*main->returnType, *main);
    if (mainType.kind != TypeKind::I32 && mainType.kind != TypeKind::Void) {
        ctx.error(QStringLiteral("E0505"), QStringLiteral("main must return int or void"), main->span);
    }
    if (!main->params.empty()) {
        ctx.error(QStringLiteral("E0505"), QStringLiteral("main must not take parameters"), main->span);
    }

    const FunctionDeclNode* setup = findRootFunctionInFile(QStringLiteral("setup"), main->span.file);
    const FunctionDeclNode* teardown = findRootFunctionInFile(QStringLiteral("teardown"), main->span.file);
    validateTestFixture(setup, QStringLiteral("setup"), ctx);
    validateTestFixture(teardown, QStringLiteral("teardown"), ctx);
    if (ctx.hasError()) {
        result.exitCode = 1;
        result.diagnostics = ctx.takeDiagnostics();
        endRun();
        return result;
    }

    QList<Diagnostic> diagnostics;
    ExecResult mainFlow = ExecResult::returned(AbelValue::makeVoid());
    bool mainAttempted = false;
    bool setupFailed = false;
    if (setup) {
        callFunction(*setup, {});
        const QList<Diagnostic> setupDiagnostics = ctx.takeDiagnostics();
        setupFailed = !setupDiagnostics.isEmpty();
        diagnostics.append(setupDiagnostics);
    }

    if (!setupFailed) {
        mainAttempted = true;
        mainFlow = callFunction(*main, {});
        diagnostics.append(ctx.takeDiagnostics());
    }

    if (teardown) {
        callFunction(*teardown, {});
        diagnostics.append(ctx.takeDiagnostics());
    }
    diagnostics.append(ctx.takeDiagnostics());

    result.returnValue = mainFlow.value;
    result.diagnostics = diagnostics;
    if (!mainAttempted) {
        result.exitCode = 1;
    } else if (mainType.kind == TypeKind::I32 && mainFlow.value.type().isInteger()) {
        result.exitCode = static_cast<int>(mainFlow.value.asInt());
    } else if (mainType.kind == TypeKind::Void) {
        result.exitCode = 0;
    } else {
        result.exitCode = 1;
    }
    if (!result.diagnostics.isEmpty() && result.exitCode == 0)
        result.exitCode = 1;
    endRun();
    return result;
}

bool Interpreter::collectStructs(const ProgramNode& program, AbelRuntimeContext& ctx)
{
    bool ok = true;
    for (const auto& decl : program.declarations) {
        auto* s = dynamic_cast<StructDeclNode*>(decl.get());
        if (!s)
            continue;
        const auto existing = m_structs.value(s->name);
        bool duplicateInModule = false;
        for (const auto& other : existing) {
            if (other.decl && sameDeclNamespace(*other.decl, *s)) {
                ctx.error(QStringLiteral("E0565"),
                          QStringLiteral("duplicate struct '%1' in package '%2' module '%3'")
                              .arg(s->name, s->packageName, s->moduleName),
                          s->span);
                ok = false;
                duplicateInModule = true;
                break;
            }
        }
        if (duplicateInModule)
            continue;
        StructRuntimeInfo info;
        info.decl = s;
        for (const auto& ctor : s->constructors) {
            bool duplicate = false;
            for (const ConstructorDeclNode* other : info.constructors) {
                if (other && sameConstructorSignature(*s, *other, *ctor)) {
                    ctx.error(QStringLiteral("E0565"),
                              QStringLiteral("duplicate constructor overload with the same signature"),
                              ctor->span);
                    ok = false;
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate)
                info.constructors.push_back(ctor.get());
        }
        for (const auto& method : s->methods) {
            bool duplicate = false;
            const auto existing = info.methods.value(method->name);
            for (const FunctionDeclNode* other : existing) {
                if (other
                    && other->isConstMethod == method->isConstMethod
                    && sameFunctionSignature(*other, *method)) {
                    ctx.error(QStringLiteral("E0565"),
                              QStringLiteral("duplicate method '%1' overload with the same signature").arg(method->name),
                              method->span);
                    ok = false;
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate)
                info.methods[method->name].push_back(method.get());
        }
        m_structs[s->name].push_back(info);
    }
    return ok;
}

bool Interpreter::collectFunctions(const ProgramNode& program, AbelRuntimeContext& ctx)
{
    bool ok = true;
    for (const auto& decl : program.declarations) {
        if (auto* fn = dynamic_cast<FunctionDeclNode*>(decl.get())) {
            const auto existing = m_functions.value(fn->name);
            bool duplicateInModule = false;
            for (const FunctionDeclNode* other : existing) {
                if (sameDeclNamespace(*other, *fn)) {
                    if (fn->name == other->name) {
                        if (sameFunctionSignature(*other, *fn)) {
                            if (fn->isOperator) {
                                ctx.error(QStringLiteral("E0506"),
                                          QStringLiteral("duplicate operator '%1' overload with the same signature in package '%2' module '%3'")
                                              .arg(fn->operatorSymbol, fn->packageName, fn->moduleName),
                                          fn->span);
                            } else {
                                ctx.error(QStringLiteral("E0506"),
                                          QStringLiteral("duplicate function '%1' overload with the same signature in package '%2' module '%3'")
                                              .arg(fn->name, fn->packageName, fn->moduleName),
                                          fn->span);
                            }
                            ok = false;
                            duplicateInModule = true;
                            break;
                        }
                        continue;
                    }
                    if (fn->isOperator && other->isOperator && fn->operatorSymbol == other->operatorSymbol) {
                        if (sameFunctionSignature(*other, *fn)) {
                            ctx.error(QStringLiteral("E0506"),
                                      QStringLiteral("duplicate operator '%1' overload with the same signature in package '%2' module '%3'")
                                          .arg(fn->operatorSymbol, fn->packageName, fn->moduleName),
                                      fn->span);
                            ok = false;
                            duplicateInModule = true;
                            break;
                        }
                        continue;
                    }
                    ctx.error(QStringLiteral("E0506"),
                              QStringLiteral("duplicate function '%1' in package '%2' module '%3'")
                                  .arg(fn->name, fn->packageName, fn->moduleName),
                              fn->span);
                    ok = false;
                    duplicateInModule = true;
                    break;
                }
            }
            if (!duplicateInModule)
                m_functions[fn->name].push_back(fn);
        }
    }
    return ok;
}

bool Interpreter::sameFunctionSignature(const FunctionDeclNode& lhs, const FunctionDeclNode& rhs)
{
    if (lhs.name != rhs.name)
        return false;
    if (lhs.params.size() != rhs.params.size())
        return false;
    for (size_t i = 0; i < lhs.params.size(); ++i) {
        if (lhs.params[i]->variadic != rhs.params[i]->variadic)
            return false;
        const AbelType lhsType = typeFromAstForDecl(*lhs.params[i]->type, lhs);
        const AbelType rhsType = typeFromAstForDecl(*rhs.params[i]->type, rhs);
        if (lhsType != rhsType)
            return false;
    }
    return true;
}

bool Interpreter::sameConstructorSignature(const StructDeclNode& owner,
                                           const ConstructorDeclNode& lhs,
                                           const ConstructorDeclNode& rhs)
{
    if (lhs.params.size() != rhs.params.size())
        return false;
    for (size_t i = 0; i < lhs.params.size(); ++i) {
        if (lhs.params[i]->variadic != rhs.params[i]->variadic)
            return false;
        const AbelType lhsType = typeFromAstForDecl(*lhs.params[i]->type, owner);
        const AbelType rhsType = typeFromAstForDecl(*rhs.params[i]->type, owner);
        if (lhsType != rhsType)
            return false;
    }
    return true;
}

bool Interpreter::collectEnums(const ProgramNode& program, AbelRuntimeContext& ctx)
{
    bool ok = true;
    for (const auto& decl : program.declarations) {
        auto* e = dynamic_cast<EnumDeclNode*>(decl.get());
        if (!e)
            continue;
        const auto existing = m_enums.value(e->name);
        bool duplicateInModule = false;
        for (const auto& other : existing) {
            if (other.decl && sameDeclNamespace(*other.decl, *e)) {
                ctx.error(QStringLiteral("E0581"),
                          QStringLiteral("duplicate enum '%1' in package '%2' module '%3'")
                              .arg(e->name, e->packageName, e->moduleName),
                          e->span);
                ok = false;
                duplicateInModule = true;
                break;
            }
        }
        if (duplicateInModule)
            continue;

        EnumRuntimeInfo info;
        info.decl = e;
        for (qsizetype i = 0; i < e->enumerators.size(); ++i) {
            const QString name = e->enumerators[i];
            if (info.values.contains(name)) {
                ctx.error(QStringLiteral("E0581"),
                          QStringLiteral("duplicate enum enumerator '%1'").arg(name),
                          e->span);
                ok = false;
                continue;
            }
            info.values.insert(name, static_cast<int>(i));
        }
        m_enums[e->name].push_back(std::move(info));
    }
    return ok;
}

bool Interpreter::collectTypeAliases(const ProgramNode& program, AbelRuntimeContext& ctx)
{
    bool ok = true;
    for (const auto& decl : program.declarations) {
        auto* alias = dynamic_cast<TypeAliasDeclNode*>(decl.get());
        if (!alias)
            continue;
        const auto existing = m_typeAliases.value(alias->name);
        bool duplicateInModule = false;
        for (const TypeAliasDeclNode* other : existing) {
            if (other && sameDeclNamespace(*other, *alias)) {
                ctx.error(QStringLiteral("E0582"),
                          QStringLiteral("duplicate type alias '%1' in package '%2' module '%3'")
                              .arg(alias->name, alias->packageName, alias->moduleName),
                          alias->span);
                ok = false;
                duplicateInModule = true;
                break;
            }
        }
        if (!duplicateInModule)
            m_typeAliases[alias->name].push_back(alias);
    }
    return ok;
}

const FunctionDeclNode* Interpreter::findRootFunction(const QString& name) const
{
    const auto candidates = m_functions.value(name);
    for (auto it = candidates.crbegin(); it != candidates.crend(); ++it) {
        const FunctionDeclNode* fn = *it;
        if (!fn->fromDependency && fn->params.empty())
            return fn;
    }
    for (auto it = candidates.crbegin(); it != candidates.crend(); ++it) {
        const FunctionDeclNode* fn = *it;
        if (!fn->fromDependency)
            return fn;
    }
    return nullptr;
}

const FunctionDeclNode* Interpreter::findRootFunctionInFile(const QString& name, const QString& file) const
{
    const auto candidates = m_functions.value(name);
    for (auto it = candidates.crbegin(); it != candidates.crend(); ++it) {
        const FunctionDeclNode* fn = *it;
        if (!fn->fromDependency && fn->span.file == file && fn->params.empty())
            return fn;
    }
    for (auto it = candidates.crbegin(); it != candidates.crend(); ++it) {
        const FunctionDeclNode* fn = *it;
        if (!fn->fromDependency && fn->span.file == file)
            return fn;
    }
    return nullptr;
}

QList<const FunctionDeclNode*> Interpreter::resolveFunctionCandidates(const QString& name) const
{
    const auto candidates = m_functions.value(name);
    if (candidates.isEmpty())
        return {};
    QList<const FunctionDeclNode*> current;
    QList<const FunctionDeclNode*> visible;
    for (const FunctionDeclNode* fn : candidates) {
        if (isDeclInCurrentModule(*fn))
            current.push_back(fn);
        else if (isDeclVisible(*fn, fn->exported))
            visible.push_back(fn);
    }
    if (!current.isEmpty())
        return current;
    return visible;
}

QList<const FunctionDeclNode*> Interpreter::resolveFunctionCandidatesInModule(const QString& moduleName, const QString& name) const
{
    const QString resolvedModuleName = resolveModuleName(moduleName);
    const auto candidates = m_functions.value(name);
    if (candidates.isEmpty())
        return {};
    QList<const FunctionDeclNode*> visible;
    for (const FunctionDeclNode* fn : candidates) {
        if (fn->moduleName == resolvedModuleName && isDeclVisible(*fn, fn->exported))
            visible.push_back(fn);
    }
    return visible;
}

const FunctionDeclNode* Interpreter::resolveFunction(const QString& name) const
{
    const auto candidates = m_functions.value(name);
    if (candidates.isEmpty())
        return nullptr;
    QList<const FunctionDeclNode*> current;
    QList<const FunctionDeclNode*> visible;
    for (const FunctionDeclNode* fn : candidates) {
        if (isDeclInCurrentModule(*fn))
            current.push_back(fn);
        else if (isDeclVisible(*fn, fn->exported))
            visible.push_back(fn);
    }
    if (current.size() == 1)
        return current.front();
    if (visible.size() == 1)
        return visible.front();
    return nullptr;
}

const FunctionDeclNode* Interpreter::resolveFunctionInModule(const QString& moduleName, const QString& name) const
{
    const QString resolvedModuleName = resolveModuleName(moduleName);
    const auto candidates = m_functions.value(name);
    if (candidates.isEmpty())
        return nullptr;
    QList<const FunctionDeclNode*> visible;
    for (const FunctionDeclNode* fn : candidates) {
        if (fn->moduleName == resolvedModuleName && isDeclVisible(*fn, fn->exported))
            visible.push_back(fn);
    }
    return visible.size() == 1 ? visible.front() : nullptr;
}

const Interpreter::StructRuntimeInfo* Interpreter::resolveStruct(const QString& name) const
{
    if (const auto qualified = splitQualifiedSymbol(name))
        return resolveStructInModule(qualified->first, qualified->second);
    return resolveStructInPackage(name, m_currentPackage);
}

const Interpreter::StructRuntimeInfo* Interpreter::resolveStructInModule(const QString& moduleName, const QString& name) const
{
    const QString resolvedModuleName = resolveModuleName(moduleName);
    auto found = m_structs.constFind(name);
    if (found == m_structs.constEnd() || found->isEmpty())
        return nullptr;
    QList<const StructRuntimeInfo*> visible;
    for (const auto& info : found.value()) {
        if (info.decl && info.decl->moduleName == resolvedModuleName && isDeclVisible(*info.decl, info.decl->exported))
            visible.push_back(&info);
    }
    return visible.size() == 1 ? visible.front() : nullptr;
}

const Interpreter::StructRuntimeInfo* Interpreter::resolveStructInPackage(const QString& name, const QString& packageName) const
{
    auto found = m_structs.constFind(name);
    if (found == m_structs.constEnd() || found->isEmpty())
        return nullptr;
    const auto& candidates = found.value();
    for (const auto& info : candidates) {
        if (info.decl && isDeclInCurrentModule(*info.decl, packageName))
            return &info;
    }
    for (const auto& info : candidates) {
        if (info.decl && isDeclVisible(*info.decl, info.decl->exported))
            return &info;
    }
    return nullptr;
}

const Interpreter::StructRuntimeInfo* Interpreter::structInfoForType(const AbelType& type) const
{
    if (type.kind != TypeKind::Struct)
        return nullptr;
    for (auto it = m_structs.constBegin(); it != m_structs.constEnd(); ++it) {
        const auto& candidates = it.value();
        for (const auto& info : candidates) {
            if (info.decl && structTypeName(*info.decl) == type.spelling)
                return &info;
        }
    }
    return nullptr;
}

const Interpreter::EnumRuntimeInfo* Interpreter::resolveEnum(const QString& name) const
{
    if (const auto qualified = splitQualifiedSymbol(name))
        return resolveEnumInModule(qualified->first, qualified->second);
    return resolveEnumInPackage(name, m_currentPackage);
}

const Interpreter::EnumRuntimeInfo* Interpreter::resolveEnumInModule(const QString& moduleName, const QString& name) const
{
    const QString resolvedModuleName = resolveModuleName(moduleName);
    auto found = m_enums.constFind(name);
    if (found == m_enums.constEnd() || found->isEmpty())
        return nullptr;
    QList<const EnumRuntimeInfo*> visible;
    for (const auto& info : found.value()) {
        if (info.decl && info.decl->moduleName == resolvedModuleName && isEnumVisible(*info.decl))
            visible.push_back(&info);
    }
    return visible.size() == 1 ? visible.front() : nullptr;
}

const Interpreter::EnumRuntimeInfo* Interpreter::resolveEnumInPackage(const QString& name, const QString& packageName) const
{
    auto found = m_enums.constFind(name);
    if (found == m_enums.constEnd() || found->isEmpty())
        return nullptr;
    const auto& candidates = found.value();
    for (const auto& info : candidates) {
        if (info.decl && isDeclInCurrentModule(*info.decl, packageName))
            return &info;
    }
    for (const auto& info : candidates) {
        if (info.decl && isEnumVisible(*info.decl))
            return &info;
    }
    return nullptr;
}

const TypeAliasDeclNode* Interpreter::resolveTypeAlias(const QString& name, const QString& packageName) const
{
    if (const auto qualified = splitQualifiedSymbol(name))
        return resolveTypeAliasInModule(qualified->first, qualified->second);
    auto found = m_typeAliases.constFind(name);
    if (found == m_typeAliases.constEnd() || found->isEmpty())
        return nullptr;
    for (const TypeAliasDeclNode* alias : found.value()) {
        if (alias && isDeclInCurrentModule(*alias, packageName))
            return alias;
    }
    QList<const TypeAliasDeclNode*> visible;
    for (const TypeAliasDeclNode* alias : found.value()) {
        if (alias && isTypeAliasVisible(*alias))
            visible.push_back(alias);
    }
    return visible.size() == 1 ? visible.front() : nullptr;
}

const TypeAliasDeclNode* Interpreter::resolveTypeAliasInModule(const QString& moduleName, const QString& name) const
{
    const QString resolvedModuleName = resolveModuleName(moduleName);
    auto found = m_typeAliases.constFind(name);
    if (found == m_typeAliases.constEnd() || found->isEmpty())
        return nullptr;
    QList<const TypeAliasDeclNode*> visible;
    for (const TypeAliasDeclNode* alias : found.value()) {
        if (alias && alias->moduleName == resolvedModuleName && isTypeAliasVisible(*alias))
            visible.push_back(alias);
    }
    return visible.size() == 1 ? visible.front() : nullptr;
}

const Interpreter::BackendRuntimeInfo* Interpreter::resolveBackend(const QString& name) const
{
    return resolveBackendInPackage(name, m_currentPackage);
}

const Interpreter::BackendRuntimeInfo* Interpreter::resolveBackendInModule(const QString& moduleName, const QString& name) const
{
    const QString resolvedModuleName = resolveModuleName(moduleName);
    auto found = m_backends.constFind(name);
    if (found == m_backends.constEnd() || found->isEmpty())
        return nullptr;
    QList<const BackendRuntimeInfo*> visible;
    for (const auto& info : found.value()) {
        if (info.decl && info.decl->moduleName == resolvedModuleName && isDeclVisible(*info.decl, info.decl->exported))
            visible.push_back(&info);
    }
    return visible.size() == 1 ? visible.front() : nullptr;
}

const Interpreter::BackendRuntimeInfo* Interpreter::resolveBackendInPackage(const QString& name, const QString& packageName) const
{
    auto found = m_backends.constFind(name);
    if (found == m_backends.constEnd() || found->isEmpty())
        return nullptr;
    const auto& candidates = found.value();
    for (const auto& info : candidates) {
        if (info.decl && isDeclInCurrentModule(*info.decl, packageName))
            return &info;
    }
    for (const auto& info : candidates) {
        if (info.decl && isDeclVisible(*info.decl, info.decl->exported))
            return &info;
    }
    return nullptr;
}

AbelType Interpreter::typeFromAstInCurrentPackage(const TypeNode& node)
{
    return typeFromAstInPackage(node, m_currentPackage);
}

AbelType Interpreter::typeFromAstInPackage(const TypeNode& node, const QString& packageName)
{
    if (node.name == QStringLiteral("vector") && node.elementType) {
        AbelType base = makeVectorType(typeFromAstInPackage(*node.elementType, packageName));
        return applyTypeDecorations(base, node);
    }
    if (node.name == QStringLiteral("func") && node.elementType) {
        std::vector<AbelType> params;
        params.reserve(node.functionParamTypes.size());
        for (const auto& param : node.functionParamTypes)
            params.push_back(typeFromAstInPackage(*param, packageName));
        AbelType base = makeFunctionType(typeFromAstInPackage(*node.elementType, packageName), std::move(params));
        return applyTypeDecorations(base, node);
    }

    if (const TypeAliasDeclNode* alias = resolveTypeAlias(node.name, packageName)) {
        const QString key = declarationQualifiedName(*alias, alias->name);
        if (m_resolvingTypeAliases.contains(key)) {
            if (m_ctx)
                m_ctx->error(QStringLiteral("E0583"), QStringLiteral("recursive type alias '%1'").arg(alias->name), node.span);
            return makeType(TypeKind::Unknown);
        }
        m_resolvingTypeAliases.insert(key);
        AbelType base = typeFromAstForDecl(*alias->targetType, *alias);
        m_resolvingTypeAliases.remove(key);
        return applyTypeDecorations(base, node);
    }

    const EnumRuntimeInfo* enumInfo = nullptr;
    if (const auto qualified = splitQualifiedSymbol(node.name))
        enumInfo = resolveEnumInModule(qualified->first, qualified->second);
    else
        enumInfo = resolveEnumInPackage(node.name, packageName);
    if (enumInfo) {
        AbelType base = makeType(TypeKind::I32, enumInfo->decl ? enumInfo->decl->name : node.name);
        return applyTypeDecorations(base, node);
    }

    AbelType base = typeFromName(node.name);
    if (base.kind == TypeKind::Struct) {
        if (const auto qualified = splitQualifiedSymbol(node.name)) {
            if (const StructRuntimeInfo* info = resolveStructInModule(qualified->first, qualified->second))
                base = makeStructType(structTypeName(*info->decl));
        } else if (const StructRuntimeInfo* info = resolveStructInPackage(node.name, packageName)) {
            base = makeStructType(structTypeName(*info->decl));
        }
    }
    return applyTypeDecorations(base, node);
}

AbelType Interpreter::typeFromAstForDecl(const TypeNode& node, const DeclNode& decl)
{
    DeclContextGuard context(m_currentPackage, m_currentModule, m_currentImports, m_currentImportAliases, decl);
    return typeFromAstInPackage(node, decl.packageName);
}

bool Interpreter::collectBackends(const ProgramNode& program, AbelRuntimeContext& ctx)
{
    bool ok = true;
    for (const auto& decl : program.declarations) {
        auto* backend = dynamic_cast<BackendBlockNode*>(decl.get());
        if (!backend)
            continue;
        const auto existing = m_backends.value(backend->name);
        bool duplicateInModule = false;
        for (const auto& other : existing) {
            if (other.decl && sameDeclNamespace(*other.decl, *backend)) {
                ctx.error(QStringLiteral("E0601"),
                          QStringLiteral("duplicate backend '%1' in package '%2' module '%3'")
                              .arg(backend->name, backend->packageName, backend->moduleName),
                          backend->span);
                ok = false;
                duplicateInModule = true;
                break;
            }
        }
        if (duplicateInModule)
            continue;
        BackendRuntimeInfo info;
        info.decl = backend;
        DeclContextGuard context(m_currentPackage, m_currentModule, m_currentImports, m_currentImportAliases, *backend);
        for (const auto& fn : backend->functions) {
            if (info.functions.contains(fn->name)) {
                ctx.error(QStringLiteral("E0602"), QStringLiteral("duplicate backend function '%1::%2'").arg(backend->name, fn->name), fn->span);
                ok = false;
                continue;
            }
            info.functions.insert(fn->name, fn.get());
            std::vector<AbelType> params;
            params.reserve(fn->params.size());
            for (const auto& param : fn->params)
                params.push_back(typeFromAstInCurrentPackage(*param->type));
            BackendFunctionDesc desc{
                backend->name,
                fn->name,
                typeFromAstInCurrentPackage(*fn->returnType),
                std::move(params),
                !fn->params.empty() && fn->params.back()->variadic,
            };
            if (const BackendFunctionDesc* existing = m_activeBackendRegistry->findFunction(backend->name, fn->name)) {
                if (existing->returnType != desc.returnType || existing->params != desc.params || existing->variadic != desc.variadic) {
                    ctx.error(QStringLiteral("E0614"),
                              QStringLiteral("backend declaration '%1::%2' does not match bound backend signature: declaration %3, bound %4")
                                  .arg(backend->name,
                                       fn->name,
                                       backendSignatureText(desc),
                                       backendSignatureText(*existing)),
                              fn->span);
                    ok = false;
                }
            } else {
                m_activeBackendRegistry->registerFunction(std::move(desc));
            }
        }
        m_backends[backend->name].push_back(info);
    }
    return ok;
}

ExecResult Interpreter::callFunction(const FunctionDeclNode& fn, const std::vector<AbelValue>& args)
{
    DeclContextGuard context(m_currentPackage, m_currentModule, m_currentImports, m_currentImportAliases, fn);
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

    struct PreparedArg {
        AbelValue value;
        AbelLocation* location = nullptr;
        bool byReference = false;
        bool isConst = false;
    };
    std::vector<PreparedArg> prepared(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
        const ParameterNode& p = *fn.params[i];
        const AbelType target = typeFromAstInCurrentPackage(*p.type);
        prepared[i].isConst = isReadOnlyBinding(target, p.type->isConst);
        if (target.isReference()) {
            if (!prepared[i].isConst || !canBindReferenceValue(target, args[i].type())) {
                error(QStringLiteral("E0541"),
                      QStringLiteral("cannot bind parameter '%1' of type %2 to %3 value")
                          .arg(p.name, target.displayName(), args[i].type().displayName()),
                      p.span);
                continue;
            }
            prepared[i].value = convertOrError(args[i], *target.pointee, p.span);
            prepared[i].location = m_ctx->createStorage(prepared[i].value);
            prepared[i].byReference = true;
        } else {
            prepared[i].value = convertOrError(args[i], target, p.span);
        }
    }
    if (m_ctx->hasError()) {
        return ExecResult::returned(AbelValue::makeUnknown());
    }

    RuntimeFrameGuard frame(*m_ctx, true, functionFrameSymbol(fn), fn.span);
    for (size_t i = 0; i < args.size(); ++i) {
        const ParameterNode& p = *fn.params[i];
        if (prepared[i].byReference)
            m_ctx->defineVariable(p.name, prepared[i].location, prepared[i].isConst, true, p.span);
        else
            m_ctx->defineValueVariable(p.name, prepared[i].value, prepared[i].isConst, p.span);
    }
    if (m_ctx->hasError()) {
        return ExecResult::returned(AbelValue::makeUnknown());
    }

    const AbelType returnType = typeFromAstInCurrentPackage(*fn.returnType);
    ExecResult flow = execBlock(*fn.body);
    if (m_ctx->hasError())
        return ExecResult::returned(AbelValue::makeUnknown());
    if (flow.kind == FlowKind::Return) {
        AbelValue converted = convertOrError(flow.value, returnType, flow.span);
        return ExecResult::returned(converted, flow.span);
    }
    if (flow.kind == FlowKind::Break || flow.kind == FlowKind::Continue) {
        error(QStringLiteral("E0532"), QStringLiteral("break/continue cannot leave function '%1'").arg(fn.name), fn.span);
        return ExecResult::returned(AbelValue::makeUnknown());
    }
    if (returnType.kind == TypeKind::Void) {
        return ExecResult::returned(AbelValue::makeVoid());
    }
    error(QStringLiteral("E0509"), QStringLiteral("function '%1' ended without return").arg(fn.name), fn.span);
    return ExecResult::returned(AbelValue::makeUnknown());
}

ExecResult Interpreter::callFunctionExpr(const FunctionDeclNode& fn,
                                         const std::vector<std::unique_ptr<ExprNode>>& args,
                                         const SourceSpan& span)
{
    DeclContextGuard context(m_currentPackage, m_currentModule, m_currentImports, m_currentImportAliases, fn);
    if (fn.debt || !fn.body) {
        error(QStringLiteral("E0539"), QStringLiteral("function '%1' has no Abel body").arg(fn.name), fn.span);
        return ExecResult::returned(AbelValue::makeUnknown());
    }
    const bool variadic = !fn.params.empty() && fn.params.back()->variadic;
    const size_t fixedCount = variadic ? fn.params.size() - 1 : fn.params.size();
    if ((!variadic && args.size() != fn.params.size()) || (variadic && args.size() < fixedCount)) {
        error(QStringLiteral("E0540"),
              QStringLiteral("function '%1' expects %2 argument(s), got %3")
                  .arg(fn.name)
                  .arg(variadic ? QStringLiteral("at least %1").arg(fixedCount) : QString::number(fn.params.size()))
                  .arg(args.size()),
              span);
        return ExecResult::returned(AbelValue::makeUnknown());
    }

    struct PreparedArg {
        AbelValue value;
        AbelLocation* location = nullptr;
        bool byReference = false;
        bool isConst = false;
    };
    std::vector<PreparedArg> prepared(fixedCount);
    for (size_t i = 0; i < fixedCount; ++i) {
        const ParameterNode& p = *fn.params[i];
        const AbelType target = typeFromAstInCurrentPackage(*p.type);
        prepared[i].isConst = isReadOnlyBinding(target, p.type->isConst);
        if (target.isReference()) {
            AbelLocation* loc = evalLocation(*args[i]);
            if (!loc)
                continue;
            if (!prepared[i].isConst && loc->isReadOnly) {
                error(QStringLiteral("E0541"),
                      QStringLiteral("non-const parameter '%1' cannot bind to const lvalue").arg(p.name),
                      args[i]->span);
                continue;
            }
            AbelValue current = loc->read();
            if (!canBindReferenceValue(target, current.type())) {
                error(QStringLiteral("E0541"),
                      QStringLiteral("cannot bind parameter '%1' of type %2 to %3 lvalue")
                          .arg(p.name, target.displayName(), current.type().displayName()),
                      args[i]->span);
                continue;
            }
            prepared[i].location = loc;
            prepared[i].byReference = true;
        } else {
            prepared[i].value = convertOrError(evalExpr(*args[i]), target, args[i]->span);
        }
    }
    std::vector<AbelValue> packed;
    if (variadic) {
        const ParameterNode& p = *fn.params.back();
        if (typeFromAstInCurrentPackage(*p.type).kind != TypeKind::Any) {
            error(QStringLiteral("E0560"), QStringLiteral("only any... variadic parameters are supported"), p.span);
        } else {
            packed.reserve(args.size() - fixedCount);
            for (size_t i = fixedCount; i < args.size(); ++i)
                packed.push_back(AbelValue::makeAny(evalExpr(*args[i])));
        }
    }
    if (m_ctx->hasError()) {
        return ExecResult::returned(AbelValue::makeUnknown());
    }

    RuntimeFrameGuard frame(*m_ctx, true, functionFrameSymbol(fn), span);
    for (size_t i = 0; i < fixedCount; ++i) {
        const ParameterNode& p = *fn.params[i];
        if (prepared[i].byReference)
            m_ctx->defineVariable(p.name, prepared[i].location, prepared[i].isConst, true, p.span);
        else
            m_ctx->defineValueVariable(p.name, prepared[i].value, prepared[i].isConst, p.span);
    }
    if (variadic) {
        const ParameterNode& p = *fn.params.back();
        m_ctx->defineValueVariable(p.name, AbelValue::makeVector(makeType(TypeKind::Any), std::move(packed)), false, p.span);
    }
    if (m_ctx->hasError()) {
        return ExecResult::returned(AbelValue::makeUnknown());
    }

    ExecResult flow = execBlock(*fn.body);
    if (m_ctx->hasError()) {
        return ExecResult::returned(AbelValue::makeUnknown());
    }

    const AbelType returnType = typeFromAstInCurrentPackage(*fn.returnType);
    if (flow.kind == FlowKind::Return) {
        ExecResult result = ExecResult::returned(convertOrError(flow.value, returnType, flow.span), flow.span);
        return result;
    }
    if (flow.kind == FlowKind::Break || flow.kind == FlowKind::Continue) {
        error(QStringLiteral("E0542"), QStringLiteral("break/continue cannot leave function '%1'").arg(fn.name), fn.span);
        return ExecResult::returned(AbelValue::makeUnknown());
    }
    if (returnType.kind == TypeKind::Void) {
        return ExecResult::returned(AbelValue::makeVoid());
    }
    error(QStringLiteral("E0543"), QStringLiteral("function '%1' ended without return").arg(fn.name), fn.span);
    return ExecResult::returned(AbelValue::makeUnknown());
}

ExecResult Interpreter::callFunctionPipeExpr(const FunctionDeclNode& fn,
                                             const ExprNode& firstArg,
                                             const std::vector<std::unique_ptr<ExprNode>>& restArgs,
                                             const SourceSpan& span)
{
    DeclContextGuard context(m_currentPackage, m_currentModule, m_currentImports, m_currentImportAliases, fn);
    if (fn.debt || !fn.body) {
        error(QStringLiteral("E0539"), QStringLiteral("function '%1' has no Abel body").arg(fn.name), fn.span);
        return ExecResult::returned(AbelValue::makeUnknown());
    }

    const size_t argc = restArgs.size() + 1;
    const bool variadic = !fn.params.empty() && fn.params.back()->variadic;
    const size_t fixedCount = variadic ? fn.params.size() - 1 : fn.params.size();
    if ((!variadic && argc != fn.params.size()) || (variadic && argc < fixedCount)) {
        error(QStringLiteral("E0540"),
              QStringLiteral("function '%1' expects %2 argument(s), got %3")
                  .arg(fn.name)
                  .arg(variadic ? QStringLiteral("at least %1").arg(fixedCount) : QString::number(fn.params.size()))
                  .arg(argc),
              span);
        return ExecResult::returned(AbelValue::makeUnknown());
    }

    auto argAt = [&](size_t index) -> const ExprNode& {
        return index == 0 ? firstArg : *restArgs[index - 1];
    };

    struct PreparedArg {
        AbelValue value;
        AbelLocation* location = nullptr;
        bool byReference = false;
        bool isConst = false;
    };
    std::vector<PreparedArg> prepared(fixedCount);
    for (size_t i = 0; i < fixedCount; ++i) {
        const ParameterNode& p = *fn.params[i];
        const AbelType target = typeFromAstInCurrentPackage(*p.type);
        const ExprNode& arg = argAt(i);
        prepared[i].isConst = isReadOnlyBinding(target, p.type->isConst);
        if (target.isReference()) {
            AbelLocation* loc = evalLocation(arg);
            if (!loc)
                continue;
            if (!prepared[i].isConst && loc->isReadOnly) {
                error(QStringLiteral("E0541"),
                      QStringLiteral("non-const parameter '%1' cannot bind to const lvalue").arg(p.name),
                      arg.span);
                continue;
            }
            AbelValue current = loc->read();
            if (!canBindReferenceValue(target, current.type())) {
                error(QStringLiteral("E0541"),
                      QStringLiteral("cannot bind parameter '%1' of type %2 to %3 lvalue")
                          .arg(p.name, target.displayName(), current.type().displayName()),
                      arg.span);
                continue;
            }
            prepared[i].location = loc;
            prepared[i].byReference = true;
        } else {
            prepared[i].value = convertOrError(evalExpr(arg), target, arg.span);
        }
    }
    std::vector<AbelValue> packed;
    if (variadic) {
        const ParameterNode& p = *fn.params.back();
        if (typeFromAstInCurrentPackage(*p.type).kind != TypeKind::Any) {
            error(QStringLiteral("E0560"), QStringLiteral("only any... variadic parameters are supported"), p.span);
        } else {
            packed.reserve(argc - fixedCount);
            for (size_t i = fixedCount; i < argc; ++i)
                packed.push_back(AbelValue::makeAny(evalExpr(argAt(i))));
        }
    }
    if (m_ctx->hasError())
        return ExecResult::returned(AbelValue::makeUnknown());

    RuntimeFrameGuard frame(*m_ctx, true, functionFrameSymbol(fn), span);
    for (size_t i = 0; i < fixedCount; ++i) {
        const ParameterNode& p = *fn.params[i];
        if (prepared[i].byReference)
            m_ctx->defineVariable(p.name, prepared[i].location, prepared[i].isConst, true, p.span);
        else
            m_ctx->defineValueVariable(p.name, prepared[i].value, prepared[i].isConst, p.span);
    }
    if (variadic) {
        const ParameterNode& p = *fn.params.back();
        m_ctx->defineValueVariable(p.name, AbelValue::makeVector(makeType(TypeKind::Any), std::move(packed)), false, p.span);
    }
    if (m_ctx->hasError()) {
        return ExecResult::returned(AbelValue::makeUnknown());
    }

    ExecResult flow = execBlock(*fn.body);
    if (m_ctx->hasError())
        return ExecResult::returned(AbelValue::makeUnknown());

    const AbelType returnType = typeFromAstInCurrentPackage(*fn.returnType);
    if (flow.kind == FlowKind::Return)
        return ExecResult::returned(convertOrError(flow.value, returnType, flow.span), flow.span);
    if (flow.kind == FlowKind::Break || flow.kind == FlowKind::Continue) {
        error(QStringLiteral("E0542"), QStringLiteral("break/continue cannot leave function '%1'").arg(fn.name), fn.span);
        return ExecResult::returned(AbelValue::makeUnknown());
    }
    if (returnType.kind == TypeKind::Void)
        return ExecResult::returned(AbelValue::makeVoid());
    error(QStringLiteral("E0543"), QStringLiteral("function '%1' ended without return").arg(fn.name), fn.span);
    return ExecResult::returned(AbelValue::makeUnknown());
}

std::vector<Interpreter::PreparedCallArg> Interpreter::prepareFunctionArgs(const std::vector<std::unique_ptr<ExprNode>>& args)
{
    std::vector<PreparedCallArg> prepared;
    prepared.reserve(args.size());
    for (const auto& arg : args) {
        PreparedCallArg out;
        out.span = arg->span;
        if (auto* field = dynamic_cast<const FieldAccessExprNode*>(arg.get()); field && !field->pointer) {
            const QString enumName = staticAccessName(*field->base);
            const bool shadowedByVariable = dynamic_cast<const NameExprNode*>(field->base.get()) && m_ctx->lookupVariable(enumName);
            if (!enumName.isEmpty() && !shadowedByVariable) {
                if (const EnumRuntimeInfo* info = resolveEnum(enumName); info && info->values.contains(field->field)) {
                    out.value = evalExpr(*arg);
                    prepared.push_back(std::move(out));
                    continue;
                }
            }
        }
        if (exprCanHaveRuntimeLocation(*arg)) {
            if (AbelLocation* loc = evalLocation(*arg)) {
                out.location = loc;
                out.isReadOnly = loc->isReadOnly;
                out.value = loc->read();
            } else {
                out.value = AbelValue::makeUnknown();
            }
        } else {
            out.value = evalExpr(*arg);
        }
        prepared.push_back(std::move(out));
    }
    return prepared;
}

std::vector<Interpreter::PreparedCallArg> Interpreter::prepareFunctionPipeArgs(
    const ExprNode& firstArg,
    const std::vector<std::unique_ptr<ExprNode>>& restArgs)
{
    auto prepareOne = [&](const ExprNode& arg) {
        PreparedCallArg out;
        out.span = arg.span;
        if (auto* field = dynamic_cast<const FieldAccessExprNode*>(&arg); field && !field->pointer) {
            const QString enumName = staticAccessName(*field->base);
            const bool shadowedByVariable = dynamic_cast<const NameExprNode*>(field->base.get()) && m_ctx->lookupVariable(enumName);
            if (!enumName.isEmpty() && !shadowedByVariable) {
                if (const EnumRuntimeInfo* info = resolveEnum(enumName); info && info->values.contains(field->field)) {
                    out.value = evalExpr(arg);
                    return out;
                }
            }
        }
        if (exprCanHaveRuntimeLocation(arg)) {
            if (AbelLocation* loc = evalLocation(arg)) {
                out.location = loc;
                out.isReadOnly = loc->isReadOnly;
                out.value = loc->read();
            } else {
                out.value = AbelValue::makeUnknown();
            }
        } else {
            out.value = evalExpr(arg);
        }
        return out;
    };

    const PreparedCallArg lhs = prepareOne(firstArg);
    std::vector<PreparedCallArg> prepared;
    const bool holes = hasPipeHole(restArgs);
    prepared.reserve(restArgs.size() + (holes ? 0 : 1));
    if (!holes)
        prepared.push_back(lhs);
    for (const auto& arg : restArgs) {
        if (isPipeHoleExpr(*arg)) {
            PreparedCallArg hole = lhs;
            hole.span = arg->span;
            prepared.push_back(hole);
        } else {
            prepared.push_back(prepareOne(*arg));
        }
    }
    return prepared;
}

Interpreter::PreparedCallArg Interpreter::prepareDefaultArgument(const DeclNode& decl,
                                                                 const std::vector<std::unique_ptr<ParameterNode>>& params,
                                                                 const std::vector<PreparedCallArg>& previousArgs,
                                                                 size_t index)
{
    PreparedCallArg out;
    const ParameterNode& param = *params[index];
    out.span = param.defaultValue ? param.defaultValue->span : param.span;
    if (!param.defaultValue)
        return out;

    DeclContextGuard context(m_currentPackage, m_currentModule, m_currentImports, m_currentImportAliases, decl);
    m_ctx->pushFrame();
    for (size_t i = 0; i < index && i < previousArgs.size(); ++i) {
        const ParameterNode& earlier = *params[i];
        if (previousArgs[i].location) {
            m_ctx->defineVariable(earlier.name,
                                  previousArgs[i].location,
                                  previousArgs[i].isReadOnly,
                                  true,
                                  earlier.span);
        } else {
            m_ctx->defineValueVariable(earlier.name,
                                       previousArgs[i].value,
                                       previousArgs[i].value.type().isConst,
                                       earlier.span);
        }
    }

    if (exprCanHaveRuntimeLocation(*param.defaultValue)) {
        if (AbelLocation* loc = evalLocation(*param.defaultValue)) {
            out.location = loc;
            out.isReadOnly = loc->isReadOnly;
            out.value = loc->read();
        } else {
            out.value = AbelValue::makeUnknown();
        }
    } else {
        out.value = evalExpr(*param.defaultValue);
    }
    m_ctx->popFrame();
    return out;
}

Interpreter::NormalizedPreparedCallArgs Interpreter::normalizePreparedCallArgsForParams(
    const DeclNode& decl,
    const QString& displayName,
    const std::vector<std::unique_ptr<ParameterNode>>& params,
    const CallExprNode& call,
    const std::vector<PreparedCallArg>& rawArgs,
    bool diagnose,
    bool evaluateDefaults,
    const std::vector<bool>* rawPipeHoles)
{
    NormalizedPreparedCallArgs out;
    Q_ASSERT(rawArgs.size() == call.args.size());
    Q_ASSERT(!rawPipeHoles || rawPipeHoles->size() == rawArgs.size());

    auto fail = [&](const SourceSpan& span, const QString& message) {
        out.ok = false;
        if (diagnose)
            error(QStringLiteral("E0596"), message, span);
    };

    const bool variadic = !params.empty() && params.back()->variadic;
    const size_t fixedCount = variadic ? params.size() - 1 : params.size();
    std::vector<std::optional<size_t>> fixedArgIndex(fixedCount);
    std::vector<PreparedCallArg> variadicTail;
    bool seenNamed = false;
    size_t nextPositional = 0;
    QSet<QString> namedSeen;

    for (size_t i = 0; i < call.args.size(); ++i) {
        if (callArgSpread(call, i)) {
            if (!variadic || nextPositional < fixedCount || seenNamed) {
                fail(call.args[i]->span, QStringLiteral("spread argument can only expand into an any... tail"));
                continue;
            }
            if (!isVectorAnyType(rawArgs[i].value.type())) {
                fail(call.args[i]->span,
                     QStringLiteral("spread argument expects vector<any>, got %1").arg(rawArgs[i].value.type().displayName()));
                continue;
            }
            auto vector = rawArgs[i].value.asVector();
            for (const AbelValue& element : vector->elements) {
                PreparedCallArg spread;
                spread.span = call.args[i]->span;
                spread.value = element.isBoxedAny() ? element.asAny()->value : element;
                variadicTail.push_back(std::move(spread));
            }
            continue;
        }
        const QString name = callArgName(call, i);
        if (!name.isEmpty()) {
            seenNamed = true;
            if (namedSeen.contains(name)) {
                fail(call.args[i]->span, QStringLiteral("duplicate named argument '%1'").arg(name));
                continue;
            }
            namedSeen.insert(name);
            std::optional<size_t> paramIndex;
            for (size_t j = 0; j < fixedCount; ++j) {
                if (params[j]->name == name) {
                    paramIndex = j;
                    break;
                }
            }
            if (!paramIndex) {
                fail(call.args[i]->span, QStringLiteral("unknown parameter name '%1' in call to '%2'").arg(name, displayName));
                continue;
            }
            if (fixedArgIndex[*paramIndex].has_value()) {
                fail(call.args[i]->span, QStringLiteral("parameter '%1' is already supplied").arg(name));
                continue;
            }
            fixedArgIndex[*paramIndex] = i;
            continue;
        }

        if (seenNamed) {
            fail(call.args[i]->span, QStringLiteral("positional argument cannot appear after named arguments"));
            continue;
        }
        if (nextPositional < fixedCount) {
            fixedArgIndex[nextPositional++] = i;
        } else if (variadic) {
            variadicTail.push_back(rawArgs[i]);
        } else {
            fail(call.args[i]->span, QStringLiteral("too many arguments in call to '%1'").arg(displayName));
        }
    }

    out.args.reserve(fixedCount + variadicTail.size());
    out.defaulted.reserve(fixedCount + variadicTail.size());
    out.pipeHoles.reserve(fixedCount + variadicTail.size());

    DeclContextGuard context(m_currentPackage, m_currentModule, m_currentImports, m_currentImportAliases, decl);
    for (size_t i = 0; i < fixedCount; ++i) {
        if (fixedArgIndex[i].has_value()) {
            const size_t argIndex = *fixedArgIndex[i];
            out.args.push_back(rawArgs[argIndex]);
            out.defaulted.push_back(false);
            out.pipeHoles.push_back(rawPipeHoles && (*rawPipeHoles)[argIndex]);
            continue;
        }
        const ParameterNode& param = *params[i];
        if (!param.defaultValue) {
            fail(call.span, QStringLiteral("missing argument for parameter '%1' in call to '%2'").arg(param.name, displayName));
            out.args.push_back(PreparedCallArg{});
            out.defaulted.push_back(false);
            out.pipeHoles.push_back(false);
            continue;
        }
        if (evaluateDefaults) {
            out.args.push_back(prepareDefaultArgument(decl, params, out.args, i));
        } else {
            PreparedCallArg placeholder;
            placeholder.span = param.defaultValue->span;
            placeholder.value = defaultValueForType(typeFromAstForDecl(*param.type, decl));
            out.args.push_back(std::move(placeholder));
        }
        out.defaulted.push_back(true);
        out.pipeHoles.push_back(false);
    }

    for (PreparedCallArg& arg : variadicTail) {
        out.args.push_back(std::move(arg));
        out.defaulted.push_back(false);
        out.pipeHoles.push_back(false);
    }

    return out;
}

std::optional<int> Interpreter::scorePreparedArgument(const AbelType& paramType, const PreparedCallArg& arg) const
{
    if (arg.value.type().kind == TypeKind::Unknown)
        return 0;
    if (paramType.isReference()) {
        if (!arg.location)
            return std::nullopt;
        const bool isConstRef = paramType.pointee && paramType.pointee->isConst;
        if (!isConstRef && arg.isReadOnly)
            return std::nullopt;
        if (!canBindReferenceValue(paramType, arg.value.type()))
            return std::nullopt;
        AbelType referred = *paramType.pointee;
        referred.isConst = false;
        return referred == arg.value.type() ? 1 : 2;
    }
    if (arg.value.type().kind == TypeKind::Any)
        return paramType.kind == TypeKind::Any ? 0 : 3;
    if (!canAssignValue(paramType, arg.value.type()))
        return std::nullopt;
    return paramType == arg.value.type() ? 0 : 1;
}

const FunctionDeclNode* Interpreter::selectFunctionOverload(const QString& displayName,
                                                            const QList<const FunctionDeclNode*>& candidates,
                                                            const std::vector<PreparedCallArg>& args,
                                                            const SourceSpan& span)
{
    const FunctionDeclNode* singleOrdinary = nullptr;
    int ordinaryCount = 0;
    for (const FunctionDeclNode* fn : candidates) {
        if (fn && !fn->isOperator) {
            singleOrdinary = fn;
            ++ordinaryCount;
        }
    }
    if (ordinaryCount == 1)
        return singleOrdinary;

    struct Match { const FunctionDeclNode* fn = nullptr; };
    QList<Match> matches;
    bool sawOrdinary = false;
    int bestScore = 1'000'000;
    for (const FunctionDeclNode* fn : candidates) {
        if (!fn || fn->isOperator)
            continue;
        sawOrdinary = true;

        const bool variadic = !fn->params.empty() && fn->params.back()->variadic;
        const size_t fixedCount = variadic ? fn->params.size() - 1 : fn->params.size();
        if ((!variadic && args.size() != fn->params.size()) || (variadic && args.size() < fixedCount))
            continue;

        int score = variadic ? 4 : 0;
        bool ok = true;
        for (size_t i = 0; i < fixedCount; ++i) {
            const AbelType paramType = typeFromAstForDecl(*fn->params[i]->type, *fn);
            const auto argScore = scorePreparedArgument(paramType, args[i]);
            if (!argScore) {
                ok = false;
                break;
            }
            score += *argScore;
        }
        if (!ok)
            continue;

        if (variadic) {
            const AbelType variadicType = typeFromAstForDecl(*fn->params.back()->type, *fn);
            if (variadicType.kind != TypeKind::Any)
                continue;
            score += static_cast<int>(args.size() - fixedCount) * 4;
        }

        if (score < bestScore) {
            bestScore = score;
            matches.clear();
            matches.push_back(Match{fn});
        } else if (score == bestScore) {
            matches.push_back(Match{fn});
        }
    }

    if (!sawOrdinary) {
        error(QStringLiteral("E0525"), QStringLiteral("function '%1' has no ordinary overloads").arg(displayName), span);
        return nullptr;
    }
    if (matches.isEmpty()) {
        error(QStringLiteral("E0525"),
              QStringLiteral("no matching function '%1' overload for %2 argument(s)")
                  .arg(displayName)
                  .arg(args.size()),
              span);
        return nullptr;
    }
    if (matches.size() > 1) {
        error(QStringLiteral("E0525"), QStringLiteral("function '%1' overload is ambiguous").arg(displayName), span);
        return nullptr;
    }
    return matches.front().fn;
}

ExecResult Interpreter::callFunctionPrepared(const FunctionDeclNode& fn,
                                             const std::vector<PreparedCallArg>& args,
                                             const SourceSpan& span)
{
    DeclContextGuard context(m_currentPackage, m_currentModule, m_currentImports, m_currentImportAliases, fn);
    if (fn.debt || !fn.body) {
        error(QStringLiteral("E0539"), QStringLiteral("function '%1' has no Abel body").arg(fn.name), fn.span);
        return ExecResult::returned(AbelValue::makeUnknown());
    }

    const bool variadic = !fn.params.empty() && fn.params.back()->variadic;
    const size_t fixedCount = variadic ? fn.params.size() - 1 : fn.params.size();
    if ((!variadic && args.size() != fn.params.size()) || (variadic && args.size() < fixedCount)) {
        error(QStringLiteral("E0540"),
              QStringLiteral("function '%1' expects %2 argument(s), got %3")
                  .arg(fn.name)
                  .arg(variadic ? QStringLiteral("at least %1").arg(fixedCount) : QString::number(fn.params.size()))
                  .arg(args.size()),
              span);
        return ExecResult::returned(AbelValue::makeUnknown());
    }

    RuntimeFrameGuard frame(*m_ctx, true, functionFrameSymbol(fn), span);

    struct BoundArg {
        AbelValue value;
        AbelLocation* location = nullptr;
        bool byReference = false;
        bool isConst = false;
    };
    std::vector<BoundArg> bound(fixedCount);
    for (size_t i = 0; i < fixedCount; ++i) {
        const ParameterNode& p = *fn.params[i];
        const AbelType target = typeFromAstInCurrentPackage(*p.type);
        bound[i].isConst = isReadOnlyBinding(target, p.type->isConst);
        if (target.isReference()) {
            if (!args[i].location) {
                error(QStringLiteral("E0541"), QStringLiteral("parameter '%1' requires lvalue").arg(p.name), args[i].span);
                continue;
            }
            if (!bound[i].isConst && args[i].isReadOnly) {
                error(QStringLiteral("E0541"),
                      QStringLiteral("non-const parameter '%1' cannot bind to const lvalue").arg(p.name),
                      args[i].span);
                continue;
            }
            if (!canBindReferenceValue(target, args[i].value.type())) {
                error(QStringLiteral("E0541"),
                      QStringLiteral("cannot bind parameter '%1' of type %2 to %3 lvalue")
                          .arg(p.name, target.displayName(), args[i].value.type().displayName()),
                      args[i].span);
                continue;
            }
            bound[i].location = args[i].location;
            bound[i].byReference = true;
        } else {
            bound[i].value = convertOrError(args[i].value, target, args[i].span);
        }
    }

    std::vector<AbelValue> packed;
    if (variadic) {
        const ParameterNode& p = *fn.params.back();
        if (typeFromAstInCurrentPackage(*p.type).kind != TypeKind::Any) {
            error(QStringLiteral("E0560"), QStringLiteral("only any... variadic parameters are supported"), p.span);
        } else {
            packed.reserve(args.size() - fixedCount);
            for (size_t i = fixedCount; i < args.size(); ++i)
                packed.push_back(AbelValue::makeAny(args[i].value));
        }
    }
    if (m_ctx->hasError())
        return ExecResult::returned(AbelValue::makeUnknown());

    for (size_t i = 0; i < fixedCount; ++i) {
        const ParameterNode& p = *fn.params[i];
        if (bound[i].byReference)
            m_ctx->defineVariable(p.name, bound[i].location, bound[i].isConst, true, p.span);
        else
            m_ctx->defineValueVariable(p.name, bound[i].value, bound[i].isConst, p.span);
    }
    if (variadic) {
        const ParameterNode& p = *fn.params.back();
        m_ctx->defineValueVariable(p.name, AbelValue::makeVector(makeType(TypeKind::Any), std::move(packed)), false, p.span);
    }
    if (m_ctx->hasError())
        return ExecResult::returned(AbelValue::makeUnknown());

    ExecResult flow = execBlock(*fn.body);
    if (m_ctx->hasError())
        return ExecResult::returned(AbelValue::makeUnknown());

    const AbelType returnType = typeFromAstInCurrentPackage(*fn.returnType);
    if (flow.kind == FlowKind::Return)
        return ExecResult::returned(convertOrError(flow.value, returnType, flow.span), flow.span);
    if (flow.kind == FlowKind::Break || flow.kind == FlowKind::Continue) {
        error(QStringLiteral("E0542"), QStringLiteral("break/continue cannot leave function '%1'").arg(fn.name), fn.span);
        return ExecResult::returned(AbelValue::makeUnknown());
    }
    if (returnType.kind == TypeKind::Void)
        return ExecResult::returned(AbelValue::makeVoid());
    error(QStringLiteral("E0543"), QStringLiteral("function '%1' ended without return").arg(fn.name), fn.span);
    return ExecResult::returned(AbelValue::makeUnknown());
}

AbelValue Interpreter::invokeFunctionValueRaw(const AbelValue& fnValue,
                                              const std::vector<AbelValue>& args,
                                              AbelRuntimeContext& ctx,
                                              const SourceSpan& span)
{
    struct ContextRestore {
        AbelRuntimeContext*& slot;
        AbelRuntimeContext* saved;
        ~ContextRestore() { slot = saved; }
    } restore{m_ctx, m_ctx};
    m_ctx = &ctx;

    if (fnValue.type().kind != TypeKind::Function || !fnValue.type().pointee) {
        ctx.error(QStringLiteral("E0580"), QStringLiteral("callee is not a function value"), span);
        return AbelValue::makeUnknown();
    }
    auto function = fnValue.asFunction();
    if (!function) {
        ctx.error(QStringLiteral("E0581"), QStringLiteral("invalid function value"), span);
        return AbelValue::makeUnknown();
    }
    if (args.size() != fnValue.type().params.size()) {
        ctx.error(QStringLiteral("E0582"), QStringLiteral("function value called with wrong argument count"), span);
        return AbelValue::makeUnknown();
    }

    std::vector<PreparedCallArg> prepared(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
        prepared[i].value = args[i];
        prepared[i].span = span;
    }
    if (function->function)
        return callFunctionPrepared(*function->function, prepared, span).value;

    if (!function->lambda || !function->lambda->ownedBody) {
        ctx.error(QStringLiteral("E0581"), QStringLiteral("invalid function value"), span);
        return AbelValue::makeUnknown();
    }

    std::vector<AbelValue> values(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
        if (fnValue.type().params[i].isReference()) {
            ctx.error(QStringLiteral("E0583"),
                      QStringLiteral("backend callable argument cannot bind reference parameter %1").arg(i),
                      span);
            continue;
        }
        values[i] = convertOrError(args[i], fnValue.type().params[i], span);
    }
    if (ctx.hasError())
        return AbelValue::makeUnknown();

    const LambdaExprNode& lambda = *function->lambda;
    DeclContextGuard context(m_currentPackage,
                             m_currentModule,
                             m_currentImports,
                             m_currentImportAliases,
                             function->packageName,
                             function->moduleName,
                             function->importedModules,
                             function->importedModuleAliases);
    CurrentStructGuard structGuard(m_currentStruct, function->currentStruct);
    RuntimeFrameGuard frame(ctx, true, lambdaFrameSymbol(), span);
    for (auto it = function->valueCaptures.constBegin(); it != function->valueCaptures.constEnd(); ++it)
        ctx.defineValueVariable(it.key(), it.value(), true, lambda.span);
    for (auto it = function->refCaptures.constBegin(); it != function->refCaptures.constEnd(); ++it)
        ctx.defineVariable(it.key(), it.value(), function->refConstness.value(it.key(), false), true, lambda.span);
    for (size_t i = 0; i < args.size(); ++i) {
        const QString& name = lambda.paramNames[static_cast<qsizetype>(i)];
        ctx.defineValueVariable(name,
                                values[i],
                                isReadOnlyBinding(fnValue.type().params[i], fnValue.type().params[i].isConst),
                                lambda.span);
    }
    if (ctx.hasError())
        return AbelValue::makeUnknown();

    ExecResult flow = execBlock(*lambda.ownedBody);
    if (ctx.hasError())
        return AbelValue::makeUnknown();

    const AbelType& returnType = *fnValue.type().pointee;
    if (flow.kind == FlowKind::Return)
        return convertOrError(flow.value, returnType, flow.span);
    if (flow.kind == FlowKind::Break || flow.kind == FlowKind::Continue) {
        ctx.error(QStringLiteral("E0584"), QStringLiteral("break/continue cannot leave lambda"), lambda.span);
        return AbelValue::makeUnknown();
    }
    if (returnType.kind == TypeKind::Void)
        return AbelValue::makeVoid();
    ctx.error(QStringLiteral("E0585"), QStringLiteral("lambda ended without return"), lambda.span);
    return AbelValue::makeUnknown();
}

ExecResult Interpreter::callFunctionOverloadExpr(const QString& displayName,
                                                 const QList<const FunctionDeclNode*>& candidates,
                                                 const std::vector<std::unique_ptr<ExprNode>>& args,
                                                 const SourceSpan& span)
{
    std::vector<PreparedCallArg> prepared = prepareFunctionArgs(args);
    if (m_ctx->hasError())
        return ExecResult::returned(AbelValue::makeUnknown());
    const FunctionDeclNode* fn = selectFunctionOverload(displayName,
                                                        candidates,
                                                        prepared,
                                                        span);
    if (!fn)
        return ExecResult::returned(AbelValue::makeUnknown());
    return callFunctionPrepared(*fn, prepared, span);
}

ExecResult Interpreter::callStructuredFunctionOverloadExpr(const QString& displayName,
                                                           const QList<const FunctionDeclNode*>& candidates,
                                                           const CallExprNode& call)
{
    std::vector<PreparedCallArg> rawArgs = prepareFunctionArgs(call.args);
    if (m_ctx->hasError())
        return ExecResult::returned(AbelValue::makeUnknown());
    return callStructuredFunctionOverloadPrepared(displayName, candidates, call, rawArgs);
}

ExecResult Interpreter::callStructuredFunctionOverloadPrepared(const QString& displayName,
                                                               const QList<const FunctionDeclNode*>& candidates,
                                                               const CallExprNode& call,
                                                               const std::vector<PreparedCallArg>& rawArgs,
                                                               const std::vector<bool>* rawPipeHoles)
{
    Q_ASSERT(!rawPipeHoles || rawPipeHoles->size() == rawArgs.size());
    auto rejectMultipleMutableRefHoles = [&](const FunctionDeclNode& fn, const NormalizedPreparedCallArgs& normalized) -> bool {
        const bool variadic = !fn.params.empty() && fn.params.back()->variadic;
        const size_t fixedCount = variadic ? fn.params.size() - 1 : fn.params.size();
        int mutableRefHoleCount = 0;
        for (size_t i = 0; i < fixedCount; ++i) {
            const AbelType paramType = typeFromAstForDecl(*fn.params[i]->type, fn);
            if (i < normalized.pipeHoles.size()
                && normalized.pipeHoles[i]
                && paramType.isReference()
                && !(paramType.pointee && paramType.pointee->isConst)) {
                ++mutableRefHoleCount;
            }
        }
        if (mutableRefHoleCount > 1) {
            error(QStringLiteral("E0583"),
                  QStringLiteral("pipe RHS cannot bind the same hole to multiple mutable reference parameters"),
                  call.span);
            return true;
        }
        return false;
    };

    const FunctionDeclNode* singleOrdinary = nullptr;
    int ordinaryCount = 0;
    for (const FunctionDeclNode* fn : candidates) {
        if (fn && !fn->isOperator) {
            singleOrdinary = fn;
            ++ordinaryCount;
        }
    }
    if (ordinaryCount == 1) {
        NormalizedPreparedCallArgs normalized = normalizePreparedCallArgsForParams(*singleOrdinary,
                                                                                   displayName,
                                                                                   singleOrdinary->params,
                                                                                   call,
                                                                                   rawArgs,
                                                                                   true,
                                                                                   true,
                                                                                   rawPipeHoles);
        if (!normalized.ok || m_ctx->hasError())
            return ExecResult::returned(AbelValue::makeUnknown());
        if (rejectMultipleMutableRefHoles(*singleOrdinary, normalized))
            return ExecResult::returned(AbelValue::makeUnknown());
        return callFunctionPrepared(*singleOrdinary, normalized.args, call.span);
    }

    struct Match {
        const FunctionDeclNode* fn = nullptr;
        NormalizedPreparedCallArgs args;
    };

    QList<Match> matches;
    bool sawOrdinary = false;
    int bestScore = 1'000'000;
    for (const FunctionDeclNode* fn : candidates) {
        if (!fn || fn->isOperator)
            continue;
        sawOrdinary = true;

        NormalizedPreparedCallArgs normalized = normalizePreparedCallArgsForParams(*fn,
                                                                                   displayName,
                                                                                   fn->params,
                                                                                   call,
                                                                                   rawArgs,
                                                                                   false,
                                                                                   false,
                                                                                   rawPipeHoles);
        if (!normalized.ok)
            continue;

        const bool variadic = !fn->params.empty() && fn->params.back()->variadic;
        const size_t fixedCount = variadic ? fn->params.size() - 1 : fn->params.size();
        int score = variadic ? 4 : 0;
        bool ok = true;
        for (size_t i = 0; i < fixedCount; ++i) {
            const AbelType paramType = typeFromAstForDecl(*fn->params[i]->type, *fn);
            const auto argScore = scorePreparedArgument(paramType, normalized.args[i]);
            if (!argScore) {
                ok = false;
                break;
            }
            score += *argScore + (normalized.defaulted[i] ? 2 : 0);
        }
        if (!ok)
            continue;

        if (variadic) {
            const AbelType variadicType = typeFromAstForDecl(*fn->params.back()->type, *fn);
            if (variadicType.kind != TypeKind::Any)
                continue;
            score += static_cast<int>(normalized.args.size() - fixedCount) * 4;
        }

        if (score < bestScore) {
            bestScore = score;
            matches.clear();
            matches.push_back(Match{fn, std::move(normalized)});
        } else if (score == bestScore) {
            matches.push_back(Match{fn, std::move(normalized)});
        }
    }

    if (!sawOrdinary) {
        error(QStringLiteral("E0525"), QStringLiteral("function '%1' has no ordinary overloads").arg(displayName), call.span);
        return ExecResult::returned(AbelValue::makeUnknown());
    }
    if (matches.isEmpty()) {
        const bool structured = callHasStructuredArgs(call);
        if (structured) {
            for (const FunctionDeclNode* fn : candidates) {
                if (!fn || fn->isOperator)
                    continue;
                normalizePreparedCallArgsForParams(*fn, displayName, fn->params, call, rawArgs, true, false, rawPipeHoles);
                if (m_ctx->hasError())
                    return ExecResult::returned(AbelValue::makeUnknown());
            }
        }
        error(QStringLiteral("E0525"),
              QStringLiteral("no matching function '%1' overload for %2 argument(s)")
                  .arg(displayName)
                  .arg(call.args.size()),
              call.span);
        return ExecResult::returned(AbelValue::makeUnknown());
    }
    if (matches.size() > 1) {
        error(QStringLiteral("E0525"), QStringLiteral("function '%1' overload is ambiguous").arg(displayName), call.span);
        return ExecResult::returned(AbelValue::makeUnknown());
    }

    Match match = std::move(matches.front());
    const FunctionDeclNode* fn = match.fn;
    NormalizedPreparedCallArgs normalized = normalizePreparedCallArgsForParams(*fn,
                                                                               displayName,
                                                                               fn->params,
                                                                               call,
                                                                               rawArgs,
                                                                               true,
                                                                               true,
                                                                               rawPipeHoles);
    if (!normalized.ok || m_ctx->hasError())
        return ExecResult::returned(AbelValue::makeUnknown());
    if (rejectMultipleMutableRefHoles(*fn, normalized))
        return ExecResult::returned(AbelValue::makeUnknown());
    return callFunctionPrepared(*fn, normalized.args, call.span);
}

ExecResult Interpreter::callFunctionOverloadPipeExpr(const QString& displayName,
                                                     const QList<const FunctionDeclNode*>& candidates,
                                                     const ExprNode& firstArg,
                                                     const std::vector<std::unique_ptr<ExprNode>>& restArgs,
                                                     const SourceSpan& span)
{
    std::vector<PreparedCallArg> prepared = prepareFunctionPipeArgs(firstArg, restArgs);
    if (m_ctx->hasError())
        return ExecResult::returned(AbelValue::makeUnknown());
    const FunctionDeclNode* fn = selectFunctionOverload(displayName, candidates, prepared, span);
    if (!fn)
        return ExecResult::returned(AbelValue::makeUnknown());
    return callFunctionPrepared(*fn, prepared, span);
}

ExecResult Interpreter::callStructFunctionPrepared(const FunctionDeclNode& fn,
                                                   AbelLocation* self,
                                                   const std::vector<PreparedCallArg>& args,
                                                   const SourceSpan& span)
{
    DeclContextGuard context(m_currentPackage, m_currentModule, m_currentImports, m_currentImportAliases, fn);
    if (!self) {
        error(QStringLiteral("E0566"), QStringLiteral("missing struct receiver for '%1'").arg(fn.name), span);
        return ExecResult::returned(AbelValue::makeUnknown());
    }
    const bool variadic = !fn.params.empty() && fn.params.back()->variadic;
    const size_t fixedCount = variadic ? fn.params.size() - 1 : fn.params.size();
    if ((!variadic && args.size() != fn.params.size()) || (variadic && args.size() < fixedCount)) {
        error(QStringLiteral("E0567"), QStringLiteral("method '%1' called with wrong argument count").arg(fn.name), span);
        return ExecResult::returned(AbelValue::makeUnknown());
    }

    AbelValue selfValue = self->read();
    if (selfValue.type().kind != TypeKind::Struct) {
        error(QStringLiteral("E0568"), QStringLiteral("method receiver is not a struct"), span);
        return ExecResult::returned(AbelValue::makeUnknown());
    }

    struct BoundArg {
        AbelValue value;
        AbelLocation* location = nullptr;
        bool byReference = false;
        bool isConst = false;
    };
    std::vector<BoundArg> bound(fixedCount);
    for (size_t i = 0; i < fixedCount; ++i) {
        const ParameterNode& p = *fn.params[i];
        const AbelType target = typeFromAstInCurrentPackage(*p.type);
        bound[i].isConst = isReadOnlyBinding(target, p.type->isConst);
        if (target.isReference()) {
            if (!args[i].location) {
                error(QStringLiteral("E0566"), QStringLiteral("method parameter '%1' requires lvalue").arg(p.name), args[i].span);
                continue;
            }
            if (!bound[i].isConst && args[i].isReadOnly) {
                error(QStringLiteral("E0566"),
                      QStringLiteral("non-const method parameter '%1' cannot bind to const lvalue").arg(p.name),
                      args[i].span);
                continue;
            }
            if (!canBindReferenceValue(target, args[i].value.type())) {
                error(QStringLiteral("E0566"),
                      QStringLiteral("cannot bind method parameter '%1' of type %2 to %3 lvalue")
                          .arg(p.name, target.displayName(), args[i].value.type().displayName()),
                      args[i].span);
                continue;
            }
            bound[i].location = args[i].location;
            bound[i].byReference = true;
        } else {
            bound[i].value = convertOrError(args[i].value, target, args[i].span);
        }
    }
    std::vector<AbelValue> packed;
    if (variadic) {
        const ParameterNode& p = *fn.params.back();
        if (typeFromAstInCurrentPackage(*p.type).kind != TypeKind::Any) {
            error(QStringLiteral("E0560"), QStringLiteral("only any... variadic parameters are supported"), p.span);
        } else {
            packed.reserve(args.size() - fixedCount);
            for (size_t i = fixedCount; i < args.size(); ++i)
                packed.push_back(AbelValue::makeAny(args[i].value));
        }
    }
    if (m_ctx->hasError())
        return ExecResult::returned(AbelValue::makeUnknown());

    CurrentStructGuard structGuard(m_currentStruct, selfValue.type().spelling);
    RuntimeFrameGuard frame(*m_ctx, true, methodFrameSymbol(fn), span);
    m_ctx->defineVariable(QStringLiteral("this"), self, fn.isConstMethod || self->isReadOnly, false, span);
    auto object = selfValue.asStruct();
    for (const auto& fieldName : object->fieldOrder)
        m_ctx->defineVariable(fieldName,
                              m_ctx->createStructFieldLocation(object.get(),
                                                               fieldName,
                                                               fn.isConstMethod || self->isReadOnly || structFieldReadOnly(selfValue.type(), fieldName),
                                                               structFieldType(selfValue.type(), fieldName)),
                              fn.isConstMethod || self->isReadOnly || structFieldReadOnly(selfValue.type(), fieldName),
                              false,
                              span);
    m_ctx->pushFrame();
    for (size_t i = 0; i < fixedCount; ++i) {
        const ParameterNode& p = *fn.params[i];
        if (bound[i].byReference)
            m_ctx->defineVariable(p.name, bound[i].location, bound[i].isConst, true, p.span);
        else
            m_ctx->defineValueVariable(p.name, bound[i].value, bound[i].isConst, p.span);
    }
    if (variadic) {
        const ParameterNode& p = *fn.params.back();
        m_ctx->defineValueVariable(p.name, AbelValue::makeVector(makeType(TypeKind::Any), std::move(packed)), false, p.span);
    }
    if (m_ctx->hasError()) {
        m_ctx->popFrame();
        return ExecResult::returned(AbelValue::makeUnknown());
    }

    ExecResult flow = execBlock(*fn.body);
    m_ctx->popFrame();
    if (m_ctx->hasError())
        return ExecResult::returned(AbelValue::makeUnknown());
    const AbelType returnType = typeFromAstInCurrentPackage(*fn.returnType);
    if (flow.kind == FlowKind::Return)
        return ExecResult::returned(convertOrError(flow.value, returnType, flow.span), flow.span);
    if (flow.kind == FlowKind::Break || flow.kind == FlowKind::Continue) {
        error(QStringLiteral("E0569"), QStringLiteral("break/continue cannot leave method '%1'").arg(fn.name), fn.span);
        return ExecResult::returned(AbelValue::makeUnknown());
    }
    if (returnType.kind == TypeKind::Void)
        return ExecResult::returned(AbelValue::makeVoid());
    error(QStringLiteral("E0569"), QStringLiteral("method '%1' ended without return").arg(fn.name), fn.span);
    return ExecResult::returned(AbelValue::makeUnknown());
}

ExecResult Interpreter::callStructFunctionOverloadExpr(const QString& displayName,
                                                       const QList<const FunctionDeclNode*>& candidates,
                                                       AbelLocation* self,
                                                       const CallExprNode& call,
                                                       const SourceSpan& span,
                                                       bool receiverPipeHole,
                                                       const std::vector<bool>* rawPipeHoles,
                                                       int totalPipeHoles)
{
    if (!self) {
        error(QStringLiteral("E0566"), QStringLiteral("missing struct receiver for '%1'").arg(displayName), span);
        return ExecResult::returned(AbelValue::makeUnknown());
    }

    AbelValue receiver = self->read();
    std::vector<PreparedCallArg> rawArgs = prepareFunctionArgs(call.args);
    if (m_ctx->hasError())
        return ExecResult::returned(AbelValue::makeUnknown());

    const auto methodUsableByReceiver = [&](const FunctionDeclNode& fn) {
        return !(self->isReadOnly && !fn.isConstMethod);
    };
    const auto rejectPipeWriteConflict = [&](const FunctionDeclNode& fn,
                                             const NormalizedPreparedCallArgs& normalized,
                                             bool diagnose) -> bool {
        if (totalPipeHoles <= 1)
            return false;
        int writeHoleCount = receiverPipeHole && !fn.isConstMethod ? 1 : 0;
        const bool variadic = !fn.params.empty() && fn.params.back()->variadic;
        const size_t fixedCount = variadic ? fn.params.size() - 1 : fn.params.size();
        for (size_t i = 0; i < fixedCount; ++i) {
            const AbelType paramType = typeFromAstForDecl(*fn.params[i]->type, fn);
            if (i < normalized.pipeHoles.size()
                && normalized.pipeHoles[i]
                && paramType.isReference()
                && !(paramType.pointee && paramType.pointee->isConst)) {
                ++writeHoleCount;
            }
        }
        if (writeHoleCount > 0 && diagnose) {
            error(QStringLiteral("E0579"),
                  QStringLiteral("pipe RHS cannot use the same hole multiple times when any use is mutable"),
                  call.span);
            return true;
        }
        return writeHoleCount > 0;
    };

    const FunctionDeclNode* onlyOrdinary = nullptr;
    int ordinaryCount = 0;
    for (const FunctionDeclNode* fn : candidates) {
        if (fn) {
            onlyOrdinary = fn;
            ++ordinaryCount;
        }
    }
    if (ordinaryCount == 1) {
        if (!methodUsableByReceiver(*onlyOrdinary)) {
            error(QStringLiteral("E0579"), QStringLiteral("method '%1' requires mutable receiver").arg(displayName), span);
            return ExecResult::returned(AbelValue::makeUnknown());
        }
        NormalizedPreparedCallArgs normalized = normalizePreparedCallArgsForParams(*onlyOrdinary,
                                                                                   displayName,
                                                                                   onlyOrdinary->params,
                                                                                   call,
                                                                                   rawArgs,
                                                                                   true,
                                                                                   true,
                                                                                   rawPipeHoles);
        if (!normalized.ok || m_ctx->hasError())
            return ExecResult::returned(AbelValue::makeUnknown());
        if (rejectPipeWriteConflict(*onlyOrdinary, normalized, true))
            return ExecResult::returned(AbelValue::makeUnknown());
        return callStructFunctionPrepared(*onlyOrdinary, self, normalized.args, span);
    }

    struct Match {
        const FunctionDeclNode* fn = nullptr;
        NormalizedPreparedCallArgs args;
    };

    QList<Match> matches;
    QList<const FunctionDeclNode*> considered;
    int bestScore = 1'000'000;
    for (const FunctionDeclNode* fn : candidates) {
        if (!fn)
            continue;
        considered.push_back(fn);
        if (!methodUsableByReceiver(*fn))
            continue;

        NormalizedPreparedCallArgs normalized = normalizePreparedCallArgsForParams(*fn,
                                                                                   displayName,
                                                                                   fn->params,
                                                                                   call,
                                                                                   rawArgs,
                                                                                   false,
                                                                                   false,
                                                                                   rawPipeHoles);
        if (!normalized.ok)
            continue;
        if (rejectPipeWriteConflict(*fn, normalized, false))
            continue;

        const bool variadic = !fn->params.empty() && fn->params.back()->variadic;
        const size_t fixedCount = variadic ? fn->params.size() - 1 : fn->params.size();

        int score = fn->isConstMethod ? 1 : 0;
        score += variadic ? 4 : 0;
        bool ok = true;
        for (size_t i = 0; i < fixedCount; ++i) {
            const AbelType paramType = typeFromAstForDecl(*fn->params[i]->type, *fn);
            const auto argScore = scorePreparedArgument(paramType, normalized.args[i]);
            if (!argScore) {
                ok = false;
                break;
            }
            score += *argScore + (normalized.defaulted[i] ? 2 : 0);
        }
        if (!ok)
            continue;

        if (variadic) {
            const AbelType variadicType = typeFromAstForDecl(*fn->params.back()->type, *fn);
            if (variadicType.kind != TypeKind::Any)
                continue;
            score += static_cast<int>(normalized.args.size() - fixedCount) * 4;
        }

        if (score < bestScore) {
            bestScore = score;
            matches.clear();
            matches.push_back(Match{fn, std::move(normalized)});
        } else if (score == bestScore) {
            matches.push_back(Match{fn, std::move(normalized)});
        }
    }

    if (matches.isEmpty()) {
        if (considered.size() == 1 && considered.front() && !methodUsableByReceiver(*considered.front())) {
            error(QStringLiteral("E0579"), QStringLiteral("method '%1' requires mutable receiver").arg(displayName), span);
            return ExecResult::returned(AbelValue::makeUnknown());
        }
        if (callHasStructuredArgs(call)) {
            for (const FunctionDeclNode* fn : considered) {
                if (!fn || !methodUsableByReceiver(*fn))
                    continue;
                normalizePreparedCallArgsForParams(*fn, displayName, fn->params, call, rawArgs, true, false, rawPipeHoles);
                if (m_ctx->hasError())
                    return ExecResult::returned(AbelValue::makeUnknown());
            }
        }
        error(QStringLiteral("E0579"),
              QStringLiteral("no matching method '%1' overload for %2 argument(s)")
                  .arg(displayName)
                  .arg(call.args.size()),
              span);
        return ExecResult::returned(AbelValue::makeUnknown());
    }
    if (matches.size() > 1) {
        error(QStringLiteral("E0579"), QStringLiteral("method '%1' overload is ambiguous").arg(displayName), span);
        return ExecResult::returned(AbelValue::makeUnknown());
    }
    Q_UNUSED(receiver);
    const FunctionDeclNode* fn = matches.front().fn;
    NormalizedPreparedCallArgs normalized = normalizePreparedCallArgsForParams(*fn,
                                                                               displayName,
                                                                               fn->params,
                                                                               call,
                                                                               rawArgs,
                                                                               true,
                                                                               true,
                                                                               rawPipeHoles);
    if (!normalized.ok || m_ctx->hasError())
        return ExecResult::returned(AbelValue::makeUnknown());
    if (rejectPipeWriteConflict(*fn, normalized, true))
        return ExecResult::returned(AbelValue::makeUnknown());
    return callStructFunctionPrepared(*fn, self, normalized.args, span);
}

const ConstructorDeclNode* Interpreter::selectConstructorOverload(const QString& displayName,
                                                                  const StructRuntimeInfo& info,
                                                                  const std::vector<PreparedCallArg>& args,
                                                                  const SourceSpan& span)
{
    if (info.constructors.size() == 1)
        return info.constructors.front();

    QList<const ConstructorDeclNode*> matches;
    int bestScore = 1'000'000;
    for (const ConstructorDeclNode* ctor : info.constructors) {
        if (!ctor)
            continue;
        const bool variadic = !ctor->params.empty() && ctor->params.back()->variadic;
        const size_t fixedCount = variadic ? ctor->params.size() - 1 : ctor->params.size();
        if ((!variadic && args.size() != ctor->params.size()) || (variadic && args.size() < fixedCount))
            continue;

        int score = variadic ? 4 : 0;
        bool ok = true;
        for (size_t i = 0; i < fixedCount; ++i) {
            const AbelType paramType = typeFromAstForDecl(*ctor->params[i]->type, *info.decl);
            const auto argScore = scorePreparedArgument(paramType, args[i]);
            if (!argScore) {
                ok = false;
                break;
            }
            score += *argScore;
        }
        if (!ok)
            continue;

        if (variadic) {
            const AbelType variadicType = typeFromAstForDecl(*ctor->params.back()->type, *info.decl);
            if (variadicType.kind != TypeKind::Any)
                continue;
            score += static_cast<int>(args.size() - fixedCount) * 4;
        }

        if (score < bestScore) {
            bestScore = score;
            matches.clear();
            matches.push_back(ctor);
        } else if (score == bestScore) {
            matches.push_back(ctor);
        }
    }

    if (matches.isEmpty()) {
        error(QStringLiteral("E0576"),
              QStringLiteral("no matching constructor '%1' overload for %2 argument(s)")
                  .arg(displayName)
                  .arg(args.size()),
              span);
        return nullptr;
    }
    if (matches.size() > 1) {
        error(QStringLiteral("E0576"), QStringLiteral("constructor '%1' overload is ambiguous").arg(displayName), span);
        return nullptr;
    }
    return matches.front();
}

AbelValue Interpreter::callFunctionValue(const AbelValue& fnValue,
                                         const std::vector<std::unique_ptr<ExprNode>>& args,
                                         const SourceSpan& span)
{
    if (fnValue.type().kind != TypeKind::Function || !fnValue.type().pointee) {
        error(QStringLiteral("E0580"), QStringLiteral("callee is not a function value"), span);
        return AbelValue::makeUnknown();
    }
    auto function = fnValue.asFunction();
    if (!function) {
        error(QStringLiteral("E0581"), QStringLiteral("invalid function value"), span);
        return AbelValue::makeUnknown();
    }
    if (function->function) {
        std::vector<PreparedCallArg> prepared = prepareFunctionArgs(args);
        if (m_ctx->hasError())
            return AbelValue::makeUnknown();
        return callFunctionPrepared(*function->function, prepared, span).value;
    }
    if (!function->lambda || !function->lambda->ownedBody) {
        error(QStringLiteral("E0581"), QStringLiteral("invalid function value"), span);
        return AbelValue::makeUnknown();
    }
    if (args.size() != fnValue.type().params.size()) {
        error(QStringLiteral("E0582"), QStringLiteral("function value called with wrong argument count"), span);
        return AbelValue::makeUnknown();
    }

    std::vector<AbelValue> values(args.size());
    std::vector<AbelLocation*> locations(args.size(), nullptr);
    std::vector<bool> byReference(args.size(), false);
    std::vector<bool> paramConst(args.size(), false);
    for (size_t i = 0; i < args.size(); ++i) {
        const AbelType& target = fnValue.type().params[i];
        paramConst[i] = isReadOnlyBinding(target, target.isConst);
        if (target.isReference()) {
            byReference[i] = true;
            AbelLocation* loc = evalLocation(*args[i]);
            if (!loc)
                continue;
            if (!paramConst[i] && loc->isReadOnly) {
                error(QStringLiteral("E0583"), QStringLiteral("non-const function parameter cannot bind to const lvalue"), args[i]->span);
                continue;
            }
            AbelValue current = loc->read();
            if (!canBindReferenceValue(target, current.type())) {
                error(QStringLiteral("E0583"),
                      QStringLiteral("cannot bind function parameter %1 to %2 lvalue")
                          .arg(target.displayName(), current.type().displayName()),
                      args[i]->span);
                continue;
            }
            locations[i] = loc;
        } else {
            values[i] = convertOrError(evalExpr(*args[i]), target, args[i]->span);
        }
    }
    if (m_ctx->hasError())
        return AbelValue::makeUnknown();

    const LambdaExprNode& lambda = *function->lambda;
    DeclContextGuard context(m_currentPackage,
                             m_currentModule,
                             m_currentImports,
                             m_currentImportAliases,
                             function->packageName,
                             function->moduleName,
                             function->importedModules,
                             function->importedModuleAliases);
    CurrentStructGuard structGuard(m_currentStruct, function->currentStruct);
    RuntimeFrameGuard frame(*m_ctx, true, lambdaFrameSymbol(), span);
    for (auto it = function->valueCaptures.constBegin(); it != function->valueCaptures.constEnd(); ++it)
        m_ctx->defineValueVariable(it.key(), it.value(), true, lambda.span);
    for (auto it = function->refCaptures.constBegin(); it != function->refCaptures.constEnd(); ++it)
        m_ctx->defineVariable(it.key(), it.value(), function->refConstness.value(it.key(), false), true, lambda.span);
    for (size_t i = 0; i < args.size(); ++i) {
        const QString& name = lambda.paramNames[static_cast<qsizetype>(i)];
        if (byReference[i])
            m_ctx->defineVariable(name, locations[i], paramConst[i], true, lambda.span);
        else
            m_ctx->defineValueVariable(name, values[i], paramConst[i], lambda.span);
    }
    if (m_ctx->hasError()) {
        return AbelValue::makeUnknown();
    }

    ExecResult flow = execBlock(*lambda.ownedBody);
    if (m_ctx->hasError())
        return AbelValue::makeUnknown();

    const AbelType& returnType = *fnValue.type().pointee;
    if (flow.kind == FlowKind::Return)
        return convertOrError(flow.value, returnType, flow.span);
    if (flow.kind == FlowKind::Break || flow.kind == FlowKind::Continue) {
        error(QStringLiteral("E0584"), QStringLiteral("break/continue cannot leave lambda"), lambda.span);
        return AbelValue::makeUnknown();
    }
    if (returnType.kind == TypeKind::Void)
        return AbelValue::makeVoid();
    error(QStringLiteral("E0585"), QStringLiteral("lambda ended without return"), lambda.span);
    return AbelValue::makeUnknown();
}

AbelValue Interpreter::callFunctionValuePipe(const AbelValue& fnValue,
                                             const ExprNode& firstArg,
                                             const std::vector<std::unique_ptr<ExprNode>>& restArgs,
                                             const SourceSpan& span)
{
    if (fnValue.type().kind != TypeKind::Function || !fnValue.type().pointee) {
        error(QStringLiteral("E0580"), QStringLiteral("callee is not a function value"), span);
        return AbelValue::makeUnknown();
    }
    auto function = fnValue.asFunction();
    if (!function) {
        error(QStringLiteral("E0581"), QStringLiteral("invalid function value"), span);
        return AbelValue::makeUnknown();
    }
    if (function->function)
        return callFunctionPipeExpr(*function->function, firstArg, restArgs, span).value;
    if (!function->lambda || !function->lambda->ownedBody) {
        error(QStringLiteral("E0581"), QStringLiteral("invalid function value"), span);
        return AbelValue::makeUnknown();
    }

    std::vector<bool> holeFlags;
    const bool holes = hasPipeHole(restArgs);
    holeFlags.reserve(restArgs.size() + (holes ? 0 : 1));
    if (!holes)
        holeFlags.push_back(false);
    for (const auto& arg : restArgs)
        holeFlags.push_back(isPipeHoleExpr(*arg));

    std::vector<PreparedCallArg> prepared = prepareFunctionPipeArgs(firstArg, restArgs);
    if (m_ctx->hasError())
        return AbelValue::makeUnknown();

    const size_t argc = prepared.size();
    if (argc != fnValue.type().params.size()) {
        error(QStringLiteral("E0582"), QStringLiteral("function value called with wrong argument count"), span);
        return AbelValue::makeUnknown();
    }

    std::vector<AbelValue> values(argc);
    std::vector<AbelLocation*> locations(argc, nullptr);
    std::vector<bool> byReference(argc, false);
    std::vector<bool> paramConst(argc, false);
    int mutableRefHoleCount = 0;
    for (size_t i = 0; i < argc; ++i) {
        const AbelType& target = fnValue.type().params[i];
        paramConst[i] = isReadOnlyBinding(target, target.isConst);
        if (target.isReference()) {
            if (i < holeFlags.size() && holeFlags[i] && !(target.pointee && target.pointee->isConst))
                ++mutableRefHoleCount;
            byReference[i] = true;
            AbelLocation* loc = prepared[i].location;
            if (!loc) {
                error(QStringLiteral("E0583"), QStringLiteral("function parameter requires lvalue"), prepared[i].span);
                continue;
            }
            if (!paramConst[i] && loc->isReadOnly) {
                error(QStringLiteral("E0583"), QStringLiteral("non-const function parameter cannot bind to const lvalue"), prepared[i].span);
                continue;
            }
            AbelValue current = loc->read();
            if (!canBindReferenceValue(target, current.type())) {
                error(QStringLiteral("E0583"),
                      QStringLiteral("cannot bind function parameter %1 to %2 lvalue")
                          .arg(target.displayName(), current.type().displayName()),
                      prepared[i].span);
                continue;
            }
            locations[i] = loc;
        } else {
            values[i] = convertOrError(prepared[i].value, target, prepared[i].span);
        }
    }
    if (mutableRefHoleCount > 1)
        error(QStringLiteral("E0583"), QStringLiteral("pipe RHS cannot bind the same hole to multiple mutable reference parameters"), span);
    if (m_ctx->hasError())
        return AbelValue::makeUnknown();

    const LambdaExprNode& lambda = *function->lambda;
    DeclContextGuard context(m_currentPackage,
                             m_currentModule,
                             m_currentImports,
                             m_currentImportAliases,
                             function->packageName,
                             function->moduleName,
                             function->importedModules,
                             function->importedModuleAliases);
    CurrentStructGuard structGuard(m_currentStruct, function->currentStruct);
    RuntimeFrameGuard frame(*m_ctx, true, lambdaFrameSymbol(), span);
    for (auto it = function->valueCaptures.constBegin(); it != function->valueCaptures.constEnd(); ++it)
        m_ctx->defineValueVariable(it.key(), it.value(), true, lambda.span);
    for (auto it = function->refCaptures.constBegin(); it != function->refCaptures.constEnd(); ++it)
        m_ctx->defineVariable(it.key(), it.value(), function->refConstness.value(it.key(), false), true, lambda.span);
    for (size_t i = 0; i < argc; ++i) {
        const QString& name = lambda.paramNames[static_cast<qsizetype>(i)];
        if (byReference[i])
            m_ctx->defineVariable(name, locations[i], paramConst[i], true, lambda.span);
        else
            m_ctx->defineValueVariable(name, values[i], paramConst[i], lambda.span);
    }
    if (m_ctx->hasError()) {
        return AbelValue::makeUnknown();
    }

    ExecResult flow = execBlock(*lambda.ownedBody);
    if (m_ctx->hasError())
        return AbelValue::makeUnknown();

    const AbelType& returnType = *fnValue.type().pointee;
    if (flow.kind == FlowKind::Return)
        return convertOrError(flow.value, returnType, flow.span);
    if (flow.kind == FlowKind::Break || flow.kind == FlowKind::Continue) {
        error(QStringLiteral("E0584"), QStringLiteral("break/continue cannot leave lambda"), lambda.span);
        return AbelValue::makeUnknown();
    }
    if (returnType.kind == TypeKind::Void)
        return AbelValue::makeVoid();
    error(QStringLiteral("E0585"), QStringLiteral("lambda ended without return"), lambda.span);
    return AbelValue::makeUnknown();
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
        return ExecResult::returned(value, stmt.span);
    }
    if (auto* s = dynamic_cast<const VarDeclStmtNode*>(&stmt))
        return execVarDecl(*s);
    if (auto* s = dynamic_cast<const TupleCastStmtNode*>(&stmt))
        return execTupleCastStmt(*s);
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
    if (auto* s = dynamic_cast<const ForStmtNode*>(&stmt))
        return execFor(*s);
    if (auto* s = dynamic_cast<const RangeForStmtNode*>(&stmt))
        return execRangeFor(*s);
    if (dynamic_cast<const BreakStmtNode*>(&stmt))
        return ExecResult::breakFlow();
    if (dynamic_cast<const ContinueStmtNode*>(&stmt))
        return ExecResult::continueFlow();

    error(QStringLiteral("E0510"), QStringLiteral("statement is not implemented in the Stage 3 interpreter"), stmt.span);
    return ExecResult::normal();
}

ExecResult Interpreter::execVarDecl(const VarDeclStmtNode& stmt)
{
    const AbelType type = typeFromAstInCurrentPackage(*stmt.type);
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
        if (!type.pointee) {
            error(QStringLiteral("E0534"), QStringLiteral("cannot bind malformed reference type"), stmt.init->span);
            return ExecResult::normal();
        }
        const bool bindingConst = isReadOnlyBinding(type, stmt.isConst || stmt.type->isConst);
        if (!bindingConst && loc->isReadOnly) {
            error(QStringLiteral("E0534"),
                  QStringLiteral("non-const reference variable '%1' cannot bind to const lvalue").arg(stmt.name),
                  stmt.init->span);
            return ExecResult::normal();
        }
        AbelValue current = loc->read();
        if (!canBindReferenceValue(type, current.type())) {
            error(QStringLiteral("E0534"),
                  QStringLiteral("cannot bind %1 to %2 lvalue")
                      .arg(type.displayName(), current.type().displayName()),
                  stmt.init->span);
            return ExecResult::normal();
        }
        m_ctx->defineVariable(stmt.name, loc, bindingConst, true, stmt.span);
        return ExecResult::normal();
    }
    AbelValue value;
    if (stmt.init && dynamic_cast<InitListExprNode*>(stmt.init.get())) {
        if (type.kind != TypeKind::Vector || !type.pointee) {
            error(QStringLiteral("E0544"), QStringLiteral("initializer list requires a vector target in the current interpreter"), stmt.span);
            return ExecResult::normal();
        }
        auto* init = dynamic_cast<InitListExprNode*>(stmt.init.get());
        std::vector<AbelValue> values;
        values.reserve(init->values.size());
        for (const auto& element : init->values)
            values.push_back(convertOrError(evalExpr(*element), *type.pointee, element->span));
        value = AbelValue::makeVector(*type.pointee, std::move(values));
    } else {
        value = stmt.init ? convertOrError(evalExpr(*stmt.init), type, stmt.init->span) : defaultConstructValue(type, stmt.span);
        if (m_ctx->hasError())
            return ExecResult::normal();
    }
    m_ctx->defineValueVariable(stmt.name, value, isReadOnlyBinding(type, stmt.isConst || stmt.type->isConst), stmt.span);
    return ExecResult::normal();
}

ExecResult Interpreter::execTupleCastStmt(const TupleCastStmtNode& stmt)
{
    const AbelValue rhs = evalExpr(*stmt.rhs);
    if (rhs.type().kind == TypeKind::Unknown)
        return ExecResult::normal();
    if (rhs.type().kind != TypeKind::Any) {
        error(QStringLiteral("E0644"),
              QStringLiteral("tuple cast source must be any containing tuple, got %1")
                  .arg(rhs.type().displayName()),
              stmt.rhs->span);
        return ExecResult::normal();
    }

    for (qsizetype i = 0; i < static_cast<qsizetype>(stmt.elements.size()); ++i) {
        const auto& element = stmt.elements[static_cast<size_t>(i)];
        if (element.skip)
            continue;
        auto item = evalDynamicIndex(rhs, i, element.span);
        if (!item)
            return ExecResult::normal();

        if (element.type) {
            AbelType target = typeFromAstInCurrentPackage(*element.type);
            if (target.isReference() && target.pointee)
                target = *target.pointee;
            AbelValue value = convertOrError(*item, target, element.span);
            if (m_ctx->hasError())
                return ExecResult::normal();
            m_ctx->defineValueVariable(element.name, value, false, element.span);
            continue;
        }

        VariableSlot* slot = m_ctx->lookupVariable(element.name);
        if (!slot || !slot->location) {
            error(QStringLiteral("E0645"),
                  QStringLiteral("tuple cast target variable '%1' is not declared")
                      .arg(element.name),
                  element.span);
            return ExecResult::normal();
        }
        if (slot->location->isReadOnly) {
            error(QStringLiteral("E0645"),
                  QStringLiteral("tuple cast target variable '%1' is const")
                      .arg(element.name),
                  element.span);
            return ExecResult::normal();
        }
        const AbelType target = slot->location->declaredType.kind != TypeKind::Unknown
            ? slot->location->declaredType
            : slot->location->read().type();
        AbelValue value = convertOrError(*item, target, element.span);
        if (m_ctx->hasError())
            return ExecResult::normal();
        slot->location->write(value);
    }
    return ExecResult::normal();
}

ExecResult Interpreter::execFor(const ForStmtNode& stmt)
{
    m_ctx->pushFrame();
    if (stmt.init) {
        ExecResult init = execStmt(*stmt.init);
        if (init.kind != FlowKind::Normal || m_ctx->hasError()) {
            m_ctx->popFrame();
            return init;
        }
    }

    for (;;) {
        if (stmt.condition) {
            bool cond = false;
            if (!requireBool(evalExpr(*stmt.condition), stmt.condition->span, cond) || !cond)
                break;
        }
        ExecResult r = execStmt(*stmt.body);
        if (r.kind == FlowKind::Return) {
            m_ctx->popFrame();
            return r;
        }
        if (r.kind == FlowKind::Break)
            break;
        if (m_ctx->hasError())
            break;
        if (stmt.step)
            evalExpr(*stmt.step);
        if (m_ctx->hasError())
            break;
        if (r.kind == FlowKind::Continue)
            continue;
    }

    m_ctx->popFrame();
    return ExecResult::normal();
}

ExecResult Interpreter::execRangeFor(const RangeForStmtNode& stmt)
{
    AbelValue range = evalExpr(*stmt.range);
    if (range.type().kind != TypeKind::Vector || !range.type().pointee) {
        error(QStringLiteral("E0561"),
              QStringLiteral("range-for requires vector, got %1").arg(range.type().displayName()),
              stmt.range->span);
        return ExecResult::normal();
    }

    bool rangeReadOnly = range.type().isConst || (range.type().pointee && range.type().pointee->isConst);
    if (exprCanHaveRuntimeLocation(*stmt.range)) {
        AbelLocation* rangeLoc = evalLocation(*stmt.range);
        if (!rangeLoc)
            return ExecResult::normal();
        range = rangeLoc->read();
        rangeReadOnly = rangeReadOnly || rangeLoc->isReadOnly;
    }
    auto vector = range.asVector();
    m_ctx->pushFrame();
    for (size_t i = 0; i < vector->elements.size(); ++i) {
        AbelLocation* loc = m_ctx->createVectorElementLocation(vector.get(), i, rangeReadOnly);
        m_ctx->pushFrame();
        m_ctx->defineVariable(stmt.variable, loc, rangeReadOnly, true, stmt.span);
        ExecResult r = execBlock(*stmt.body);
        m_ctx->popFrame();
        if (r.kind == FlowKind::Return) {
            m_ctx->popFrame();
            return r;
        }
        if (r.kind == FlowKind::Break)
            break;
        if (m_ctx->hasError())
            break;
        if (r.kind == FlowKind::Continue)
            continue;
    }
    m_ctx->popFrame();
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
        if (e->name == QStringLiteral("_") && m_hasPipeHoleArg)
            return m_pipeHoleArg.value;
        if (isSourceLocationBuiltinName(e->name))
            return evalSourceLocationBuiltin(e->name, e->span);

        const VariableSlot* slot = m_ctx->lookupVariable(e->name);
        if (!slot) {
            const QList<const FunctionDeclNode*> candidates = resolveFunctionCandidates(e->name);
            QList<const FunctionDeclNode*> valueCandidates;
            for (const FunctionDeclNode* fn : candidates) {
                if (fn && !fn->isOperator)
                    valueCandidates.push_back(fn);
            }
            if (valueCandidates.size() == 1)
                return makeFunctionValue(*valueCandidates.front());
            if (valueCandidates.size() > 1)
                error(QStringLiteral("E0513"), QStringLiteral("function '%1' is overloaded; cannot infer function value type").arg(e->name), e->span);
            else
                error(QStringLiteral("E0513"), QStringLiteral("unknown variable '%1'").arg(e->name), e->span);
            return AbelValue::makeUnknown();
        }
        return slot->location ? slot->location->read() : AbelValue::makeUnknown();
    }
    if (dynamic_cast<const ThisExprNode*>(&expr)) {
        const VariableSlot* slot = m_ctx->lookupVariable(QStringLiteral("this"));
        if (!slot || !slot->location) {
            error(QStringLiteral("E0570"), QStringLiteral("this is not available here"), expr.span);
            return AbelValue::makeUnknown();
        }
        return slot->location->read();
    }
    if (auto* e = dynamic_cast<const UnaryExprNode*>(&expr))
        return evalUnary(*e);
    if (auto* e = dynamic_cast<const BinaryExprNode*>(&expr))
        return evalBinary(*e);
    if (auto* e = dynamic_cast<const CastExprNode*>(&expr))
        return evalCast(*e);
    if (auto* e = dynamic_cast<const AssignExprNode*>(&expr))
        return evalAssignment(*e);
    if (auto* e = dynamic_cast<const CallExprNode*>(&expr))
        return evalCall(*e);
    if (auto* e = dynamic_cast<const LambdaExprNode*>(&expr))
        return evalLambda(*e);
    if (auto* e = dynamic_cast<const DoExprNode*>(&expr))
        return evalDoExpression(*e);
    if (auto* e = dynamic_cast<const AnyTupleLiteralExprNode*>(&expr))
        return evalAnyTupleLiteral(*e);
    if (auto* e = dynamic_cast<const StrMapLiteralExprNode*>(&expr))
        return evalStrMapLiteral(*e);
    if (auto* e = dynamic_cast<const IndexExprNode*>(&expr)) {
        AbelLocation* loc = evalLocation(*e);
        return loc ? loc->read() : AbelValue::makeUnknown();
    }
    if (auto* e = dynamic_cast<const FieldAccessExprNode*>(&expr)) {
        if (!e->pointer) {
            const QString enumName = staticAccessName(*e->base);
            if (!enumName.isEmpty()) {
                if (dynamic_cast<const NameExprNode*>(e->base.get()) && m_ctx->lookupVariable(enumName))
                    goto normalFieldEval;
                if (const EnumRuntimeInfo* info = resolveEnum(enumName)) {
                    const auto found = info->values.constFind(e->field);
                    if (found != info->values.constEnd())
                        return AbelValue::makeInt(found.value(), TypeKind::I32);
                    error(QStringLiteral("E0581"), QStringLiteral("enum '%1' has no enumerator '%2'").arg(enumName, e->field), e->span);
                    return AbelValue::makeUnknown();
                }
            }
        }
normalFieldEval:
        AbelLocation* loc = evalLocation(*e);
        return loc ? loc->read() : AbelValue::makeUnknown();
    }
    if (dynamic_cast<const InitListExprNode*>(&expr)) {
        error(QStringLiteral("E0545"), QStringLiteral("initializer list needs a target type"), expr.span);
        return AbelValue::makeUnknown();
    }
    if (auto* access = dynamic_cast<const StaticAccessExprNode*>(&expr)) {
        const QString moduleName = staticAccessModuleName(*access);
        if (!moduleName.isEmpty()) {
            const QList<const FunctionDeclNode*> candidates = resolveFunctionCandidatesInModule(moduleName, access->member);
            QList<const FunctionDeclNode*> valueCandidates;
            for (const FunctionDeclNode* fn : candidates) {
                if (fn && !fn->isOperator)
                    valueCandidates.push_back(fn);
            }
            if (valueCandidates.size() == 1)
                return makeFunctionValue(*valueCandidates.front());
            if (valueCandidates.size() > 1)
                error(QStringLiteral("E0514"),
                      QStringLiteral("function '%1::%2' is overloaded; cannot infer function value type").arg(moduleName, access->member),
                      expr.span);
            else
                error(QStringLiteral("E0514"), QStringLiteral("static/backend access is parsed but not executable in Stage 3"), expr.span);
        } else {
            error(QStringLiteral("E0514"), QStringLiteral("static/backend access is parsed but not executable in Stage 3"), expr.span);
        }
        return AbelValue::makeUnknown();
    }

    error(QStringLiteral("E0515"), QStringLiteral("expression is not implemented in the Stage 3 interpreter"), expr.span);
    return AbelValue::makeUnknown();
}

AbelLocation* Interpreter::evalLocation(const ExprNode& expr)
{
    if (dynamic_cast<const ThisExprNode*>(&expr)) {
        VariableSlot* slot = m_ctx->lookupVariable(QStringLiteral("this"));
        if (!slot || !slot->location) {
            error(QStringLiteral("E0570"), QStringLiteral("this is not available here"), expr.span);
            return nullptr;
        }
        return slot->location;
    }
    if (auto* e = dynamic_cast<const NameExprNode*>(&expr)) {
        if (e->name == QStringLiteral("_") && m_hasPipeHoleArg) {
            if (m_pipeHoleArg.location)
                return m_pipeHoleArg.location;
            if (!m_pipeHoleTempLocation)
                m_pipeHoleTempLocation = m_ctx->createStorage(m_pipeHoleArg.value, m_pipeHoleArg.value.type().isConst);
            return m_pipeHoleTempLocation;
        }
        VariableSlot* slot = m_ctx->lookupVariable(e->name);
        if (!slot || !slot->location) {
            error(QStringLiteral("E0535"), QStringLiteral("unknown variable '%1'").arg(e->name), e->span);
            return nullptr;
        }
        return slot->location;
    }
    if (auto* e = dynamic_cast<const FieldAccessExprNode*>(&expr)) {
        AbelValue base;
        AbelStructValue* object = nullptr;
        bool baseReadOnly = false;
        if (e->pointer) {
            AbelValue ptr = evalExpr(*e->base);
            if (!ptr.type().isPointer() || !ptr.type().pointee || ptr.type().pointee->kind != TypeKind::Struct) {
                error(QStringLiteral("E0571"), QStringLiteral("operator -> requires pointer to struct"), e->span);
                return nullptr;
            }
            AbelLocation* pointee = ptr.asPointer();
            if (!pointee) {
                error(QStringLiteral("E0572"), QStringLiteral("cannot dereference nullptr struct pointer"), e->span);
                return nullptr;
            }
            baseReadOnly = pointee->isReadOnly || ptr.type().pointee->isConst;
            base = pointee->read();
        } else {
            AbelLocation* baseLoc = evalLocation(*e->base);
            if (!baseLoc)
                return nullptr;
            baseReadOnly = baseLoc->isReadOnly;
            base = baseLoc->read();
        }
        if (base.type().kind != TypeKind::Struct) {
            error(QStringLiteral("E0573"), QStringLiteral("field access requires struct"), e->span);
            return nullptr;
        }
        object = base.asStruct().get();
        if (!object->fields.contains(e->field)) {
            error(QStringLiteral("E0574"), QStringLiteral("unknown field '%1'").arg(e->field), e->span);
            return nullptr;
        }
        const StructRuntimeInfo* info = structInfoForType(base.type());
        const QString baseStructName = info && info->decl ? structTypeName(*info->decl) : base.type().spelling;
        if (structFieldPrivate(base.type(), e->field) && m_currentStruct != base.type().spelling && m_currentStruct != baseStructName) {
            error(QStringLiteral("E0574"), QStringLiteral("field '%1' is private").arg(e->field), e->span);
            return nullptr;
        }
        const bool fieldReadOnly = baseReadOnly || base.type().isConst || structFieldReadOnly(base.type(), e->field);
        return m_ctx->createStructFieldLocation(object, e->field, fieldReadOnly, structFieldType(base.type(), e->field));
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
        return readonlyAlias(*m_ctx, loc, ptr.type().pointee && ptr.type().pointee->isConst);
    }
    if (auto* e = dynamic_cast<const IndexExprNode*>(&expr)) {
        AbelValue base;
        bool baseReadOnly = false;
        if (exprCanHaveRuntimeLocation(*e->base)) {
            AbelLocation* baseLoc = evalLocation(*e->base);
            if (!baseLoc)
                return nullptr;
            baseReadOnly = baseLoc->isReadOnly;
            base = baseLoc->read();
        } else {
            base = evalExpr(*e->base);
        }
        const bool baseIsAny = base.type().kind == TypeKind::Any;
        AbelValue rawBase = unboxAnyValue(base);
        if (rawBase.isDynamicObject()) {
            auto object = rawBase.asDynamicObject();
            AbelValue key = unboxAnyValue(evalExpr(*e->index));
            if (!object || !object->get) {
                error(QStringLiteral("E0546"),
                      QStringLiteral("dynamic object '%1' does not support operator []")
                          .arg(object ? object->kind : QStringLiteral("null")),
                      e->span);
                return nullptr;
            }
            auto read = [this, object, key, span = e->span]() {
                return object->get(key, *m_ctx, span);
            };
            auto write = [this, object, key, span = e->span](const AbelValue& value) {
                if (!object->set) {
                    error(QStringLiteral("E0546"),
                          QStringLiteral("dynamic object '%1' does not support operator []=")
                              .arg(object ? object->kind : QStringLiteral("null")),
                          span);
                    return;
                }
                object->set(key, value, *m_ctx, span);
            };
            return m_ctx->createCustomLocation(std::move(read),
                                               std::move(write),
                                               baseReadOnly,
                                               makeType(TypeKind::Any));
        }
        if (rawBase.type().kind != TypeKind::Vector || !rawBase.type().pointee) {
            error(QStringLiteral("E0546"),
                  baseIsAny
                      ? QStringLiteral("dynamic index requires any containing vector, got any containing %1")
                            .arg(rawBase.type().displayName())
                      : QStringLiteral("indexing requires vector value"),
                  e->span);
            return nullptr;
        }
        m_ctx->createStorage(rawBase);
        AbelValue index = unboxAnyValue(evalExpr(*e->index));
        if (!index.type().isInteger()) {
            error(QStringLiteral("E0529"),
                  QStringLiteral("%1 index must be integer, got %2")
                      .arg(baseIsAny ? QStringLiteral("dynamic") : QStringLiteral("vector"),
                           index.type().displayName()),
                  e->index->span);
            return nullptr;
        }
        const qint64 idx = index.asInt();
        auto vector = rawBase.asVector();
        if (idx < 0 || static_cast<size_t>(idx) >= vector->elements.size()) {
            error(QStringLiteral("E0547"), QStringLiteral("vector index out of range"), e->span);
            return nullptr;
        }
        return m_ctx->createVectorElementLocation(vector.get(), static_cast<size_t>(idx), vectorElementReadOnly(rawBase, baseReadOnly));
    }
    if (auto* e = dynamic_cast<const CallExprNode*>(&expr)) {
        auto* field = dynamic_cast<FieldAccessExprNode*>(e->callee.get());
        if (field && (field->field == QStringLiteral("front") || field->field == QStringLiteral("back"))) {
            if (!e->args.empty()) {
                error(QStringLiteral("E0562"), QStringLiteral("vector.%1 expects no arguments").arg(field->field), e->span);
                return nullptr;
            }
            AbelValue base;
            bool baseReadOnly = false;
            if (exprCanHaveRuntimeLocation(*field->base)) {
                AbelLocation* baseLoc = evalLocation(*field->base);
                if (!baseLoc)
                    return nullptr;
                baseReadOnly = baseLoc->isReadOnly;
                base = baseLoc->read();
            } else {
                base = evalExpr(*field->base);
            }
            if (base.type().kind != TypeKind::Vector || !base.type().pointee) {
                error(QStringLiteral("E0563"), QStringLiteral("vector.%1 requires vector receiver").arg(field->field), field->span);
                return nullptr;
            }
            m_ctx->createStorage(base);
            auto vector = base.asVector();
            if (vector->elements.empty()) {
                error(QStringLiteral("E0564"), QStringLiteral("cannot read %1 of empty vector").arg(field->field), field->span);
                return nullptr;
            }
            const size_t index = field->field == QStringLiteral("front") ? 0 : vector->elements.size() - 1;
            return m_ctx->createVectorElementLocation(vector.get(), index, vectorElementReadOnly(base, baseReadOnly));
        }
    }
    error(QStringLiteral("E0538"), QStringLiteral("expression is not an lvalue"), expr.span);
    return nullptr;
}

AbelValue Interpreter::evalBinary(const BinaryExprNode& expr)
{
    if (expr.op == QStringLiteral("|>"))
        return evalPipe(expr);

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
    auto evalBuiltinDynamic = [&](const AbelValue& rawLhs, const AbelValue& rawRhs) -> std::optional<AbelValue> {
        if (op == QStringLiteral("==") || op == QStringLiteral("!=")) {
            auto dynamicEquals = [&](const AbelValue& lhsValue, const AbelValue& rhsValue) -> std::optional<bool> {
                if (!lhsValue.isDynamicObject())
                    return std::nullopt;
                auto object = lhsValue.asDynamicObject();
                if (!object || !object->equals)
                    return std::nullopt;
                return object->equals(rhsValue, *m_ctx, expr.span);
            };
            std::optional<bool> eq = dynamicEquals(rawLhs, rawRhs);
            if (!eq && rawRhs.isDynamicObject())
                eq = dynamicEquals(rawRhs, rawLhs);
            if (eq)
                return AbelValue::makeBool(op == QStringLiteral("==") ? *eq : !*eq);
            if (m_ctx->hasError())
                return AbelValue::makeUnknown();
        }

        if (op == QStringLiteral("+") && rawLhs.type().kind == TypeKind::Str && rawRhs.type().kind == TypeKind::Str)
            return AbelValue::makeString(rawLhs.asString() + rawRhs.asString());

        if (op == QStringLiteral("==") || op == QStringLiteral("!=")) {
            const bool pointerNull =
                (rawLhs.type().kind == TypeKind::Pointer && rawRhs.type().kind == TypeKind::Nullptr)
                || (rawLhs.type().kind == TypeKind::Nullptr && rawRhs.type().kind == TypeKind::Pointer);
            if (pointerNull) {
                AbelLocation* ptr = rawLhs.type().kind == TypeKind::Pointer ? rawLhs.asPointer() : rawRhs.asPointer();
                const bool eq = ptr == nullptr;
                return AbelValue::makeBool(op == QStringLiteral("==") ? eq : !eq);
            }
            if (rawLhs.type().isNumeric() && rawRhs.type().isNumeric()) {
                const bool useDouble = rawLhs.type().kind == TypeKind::F64 || rawRhs.type().kind == TypeKind::F64;
                const bool useUnsigned = !useDouble && (rawLhs.type().isUnsignedInteger() || rawRhs.type().isUnsignedInteger());
                const bool eq = useDouble
                    ? rawLhs.asDouble() == rawRhs.asDouble()
                    : useUnsigned
                        ? static_cast<quint64>(rawLhs.asInt()) == static_cast<quint64>(rawRhs.asInt())
                        : rawLhs.asInt() == rawRhs.asInt();
                return AbelValue::makeBool(op == QStringLiteral("==") ? eq : !eq);
            }
            if (rawLhs.type().kind == rawRhs.type().kind && isBuiltinEqualityComparable(rawLhs.type(), rawRhs.type())) {
                const bool eq = abelValueEquals(rawLhs, rawRhs);
                return AbelValue::makeBool(op == QStringLiteral("==") ? eq : !eq);
            }
            return std::nullopt;
        }

        if (!rawLhs.type().isNumeric() || !rawRhs.type().isNumeric())
            return std::nullopt;

        const bool useDouble = rawLhs.type().kind == TypeKind::F64 || rawRhs.type().kind == TypeKind::F64;
        if (useDouble) {
            const double a = rawLhs.asDouble();
            const double b = rawRhs.asDouble();
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
            return std::nullopt;
        }

        const TypeKind resultKind = numericBinaryResultKind(rawLhs.type(), rawRhs.type());
        const bool useUnsigned = rawLhs.type().isUnsignedInteger() || rawRhs.type().isUnsignedInteger();
        if (useUnsigned) {
            const quint64 a = static_cast<quint64>(rawLhs.asInt());
            const quint64 b = static_cast<quint64>(rawRhs.asInt());
            if (op == QStringLiteral("+")) return AbelValue::makeInt(static_cast<qint64>(a + b), resultKind);
            if (op == QStringLiteral("-")) return AbelValue::makeInt(static_cast<qint64>(a - b), resultKind);
            if (op == QStringLiteral("*")) return AbelValue::makeInt(static_cast<qint64>(a * b), resultKind);
            if (op == QStringLiteral("/")) {
                if (b == 0) {
                    error(QStringLiteral("E0517"), QStringLiteral("division by zero"), expr.span);
                    return AbelValue::makeUnknown();
                }
                return AbelValue::makeInt(static_cast<qint64>(a / b), resultKind);
            }
            if (op == QStringLiteral("%") || op == QStringLiteral("%%")) {
                if (b == 0) {
                    error(QStringLiteral("E0518"), QStringLiteral("modulo by zero"), expr.span);
                    return AbelValue::makeUnknown();
                }
                return AbelValue::makeInt(static_cast<qint64>(a % b), resultKind);
            }
            if (op == QStringLiteral("**")) {
                quint64 out = 1;
                for (quint64 i = 0; i < b; ++i)
                    out *= a;
                return AbelValue::makeInt(static_cast<qint64>(out), resultKind);
            }
            if (op == QStringLiteral("<")) return AbelValue::makeBool(a < b);
            if (op == QStringLiteral("<=")) return AbelValue::makeBool(a <= b);
            if (op == QStringLiteral(">")) return AbelValue::makeBool(a > b);
            if (op == QStringLiteral(">=")) return AbelValue::makeBool(a >= b);
            if (op == QStringLiteral("<?")) return AbelValue::makeInt(static_cast<qint64>(a < b ? a : b), resultKind);
            if (op == QStringLiteral(">?")) return AbelValue::makeInt(static_cast<qint64>(a > b ? a : b), resultKind);
            return std::nullopt;
        }

        const qint64 a = rawLhs.asInt();
        const qint64 b = rawRhs.asInt();
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
        return std::nullopt;
    };

    if (lhs.type().kind == TypeKind::Any || rhs.type().kind == TypeKind::Any) {
        AbelValue rawLhs = unboxAnyValue(lhs);
        AbelValue rawRhs = unboxAnyValue(rhs);
        if (auto builtin = evalBuiltinDynamic(rawLhs, rawRhs))
            return *builtin;
        if (m_ctx->hasError())
            return AbelValue::makeUnknown();
        if (auto overloaded = evalUserBinaryOperator(op, lhs, rhs, expr.span))
            return *overloaded;
        error(QStringLiteral("E0521"),
              QStringLiteral("dynamic operator '%1' failed for %2 and %3")
                  .arg(op, rawLhs.type().displayName(), rawRhs.type().displayName()),
              expr.span);
        return AbelValue::makeUnknown();
    }

    if (op == QStringLiteral("+") && lhs.type().kind == TypeKind::Str && rhs.type().kind == TypeKind::Str)
        return AbelValue::makeString(lhs.asString() + rhs.asString());

    if ((op == QStringLiteral("==") || op == QStringLiteral("!="))
        && lhs.type().kind == rhs.type().kind
        && isBuiltinEqualityComparable(lhs.type(), rhs.type())) {
        bool eq = false;
        switch (lhs.type().kind) {
        case TypeKind::Bool: eq = lhs.asBool() == rhs.asBool(); break;
        case TypeKind::I8:
        case TypeKind::I16:
        case TypeKind::I32:
        case TypeKind::I64: eq = lhs.asInt() == rhs.asInt(); break;
        case TypeKind::U8:
        case TypeKind::U16:
        case TypeKind::U32:
        case TypeKind::U64: eq = static_cast<quint64>(lhs.asInt()) == static_cast<quint64>(rhs.asInt()); break;
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
        if (!lhs.type().isNumeric() || !rhs.type().isNumeric()) {
            if (auto overloaded = evalUserBinaryOperator(op, lhs, rhs, expr.span))
                return *overloaded;
            error(QStringLiteral("E0516"),
                  QStringLiteral("cannot compare %1 and %2").arg(lhs.type().displayName(), rhs.type().displayName()),
                  expr.span);
            return AbelValue::makeUnknown();
        }
    }

    if (!lhs.type().isNumeric() || !rhs.type().isNumeric()) {
        if (auto overloaded = evalUserBinaryOperator(op, lhs, rhs, expr.span))
            return *overloaded;
        error(QStringLiteral("E0516"), QStringLiteral("operator '%1' requires numeric operands").arg(op), expr.span);
        return AbelValue::makeUnknown();
    }

    const bool useDouble = lhs.type().kind == TypeKind::F64 || rhs.type().kind == TypeKind::F64;
    if (op == QStringLiteral("==") || op == QStringLiteral("!=")) {
        const bool useUnsigned = !useDouble && (lhs.type().isUnsignedInteger() || rhs.type().isUnsignedInteger());
        const bool eq = useDouble
            ? lhs.asDouble() == rhs.asDouble()
            : useUnsigned
                ? static_cast<quint64>(lhs.asInt()) == static_cast<quint64>(rhs.asInt())
                : lhs.asInt() == rhs.asInt();
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
        const TypeKind resultKind = numericBinaryResultKind(lhs.type(), rhs.type());
        const bool useUnsigned = lhs.type().isUnsignedInteger() || rhs.type().isUnsignedInteger();
        if (useUnsigned) {
            const quint64 a = static_cast<quint64>(lhs.asInt());
            const quint64 b = static_cast<quint64>(rhs.asInt());
            if (op == QStringLiteral("+")) return AbelValue::makeInt(static_cast<qint64>(a + b), resultKind);
            if (op == QStringLiteral("-")) return AbelValue::makeInt(static_cast<qint64>(a - b), resultKind);
            if (op == QStringLiteral("*")) return AbelValue::makeInt(static_cast<qint64>(a * b), resultKind);
            if (op == QStringLiteral("/")) {
                if (b == 0) {
                    error(QStringLiteral("E0517"), QStringLiteral("division by zero"), expr.span);
                    return AbelValue::makeUnknown();
                }
                return AbelValue::makeInt(static_cast<qint64>(a / b), resultKind);
            }
            if (op == QStringLiteral("%") || op == QStringLiteral("%%")) {
                if (b == 0) {
                    error(QStringLiteral("E0518"), QStringLiteral("modulo by zero"), expr.span);
                    return AbelValue::makeUnknown();
                }
                return AbelValue::makeInt(static_cast<qint64>(a % b), resultKind);
            }
            if (op == QStringLiteral("**")) {
                quint64 out = 1;
                for (quint64 i = 0; i < b; ++i)
                    out *= a;
                return AbelValue::makeInt(static_cast<qint64>(out), resultKind);
            }
            if (op == QStringLiteral("<")) return AbelValue::makeBool(a < b);
            if (op == QStringLiteral("<=")) return AbelValue::makeBool(a <= b);
            if (op == QStringLiteral(">")) return AbelValue::makeBool(a > b);
            if (op == QStringLiteral(">=")) return AbelValue::makeBool(a >= b);
            if (op == QStringLiteral("<?")) return AbelValue::makeInt(static_cast<qint64>(a < b ? a : b), resultKind);
            if (op == QStringLiteral(">?")) return AbelValue::makeInt(static_cast<qint64>(a > b ? a : b), resultKind);
        }
        const qint64 a = lhs.asInt();
        const qint64 b = rhs.asInt();
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

std::optional<AbelValue> Interpreter::evalUserBinaryOperator(const QString& op,
                                                             const AbelValue& lhs,
                                                             const AbelValue& rhs,
                                                             const SourceSpan& span)
{
    const QString name = QStringLiteral("operator ") + op;
    const QList<const FunctionDeclNode*> candidates = resolveFunctionCandidates(name);
    if (candidates.isEmpty())
        return std::nullopt;

    struct Match {
        const FunctionDeclNode* fn = nullptr;
    };

    QList<Match> matches;
    bool sawOperator = false;
    int bestScore = 1'000'000;
    std::vector<PreparedCallArg> prepared(2);
    prepared[0].value = lhs;
    prepared[0].span = span;
    prepared[1].value = rhs;
    prepared[1].span = span;
    for (const FunctionDeclNode* fn : candidates) {
        if (!fn || !fn->isOperator || fn->operatorSymbol != op)
            continue;
        sawOperator = true;
        if (fn->params.size() != 2 || (!fn->params.empty() && fn->params.back()->variadic))
            continue;
        const AbelType lhsParam = typeFromAstForDecl(*fn->params[0]->type, *fn);
        const AbelType rhsParam = typeFromAstForDecl(*fn->params[1]->type, *fn);
        const auto lhsScore = scoreValueArgument(lhsParam, lhs);
        const auto rhsScore = scoreValueArgument(rhsParam, rhs);
        if (!lhsScore || !rhsScore)
            continue;
        const int score = *lhsScore + *rhsScore;
        if (score < bestScore) {
            bestScore = score;
            matches.clear();
            matches.push_back(Match{fn});
        } else if (score == bestScore) {
            matches.push_back(Match{fn});
        }
    }

    if (!sawOperator) {
        error(QStringLiteral("E0521"), QStringLiteral("function '%1' is not a valid operator overload").arg(name), span);
        return AbelValue::makeUnknown();
    }
    if (matches.isEmpty()) {
        error(QStringLiteral("E0521"),
              QStringLiteral("no matching operator '%1' overload for %2 and %3")
                  .arg(op, lhs.type().displayName(), rhs.type().displayName()),
              span);
        return AbelValue::makeUnknown();
    }
    if (matches.size() > 1) {
        error(QStringLiteral("E0521"),
              QStringLiteral("operator '%1' is ambiguous for %2 and %3")
                  .arg(op, lhs.type().displayName(), rhs.type().displayName()),
              span);
        return AbelValue::makeUnknown();
    }

    const Match match = matches.front();
    const FunctionDeclNode* fn = match.fn;
    return callFunctionPrepared(*fn, prepared, span).value;
}

std::optional<AbelValue> Interpreter::evalUserCompoundAssignmentOperator(const QString& op,
                                                                         AbelLocation& lhsLocation,
                                                                         const AbelValue& lhs,
                                                                         const AbelValue& rhs,
                                                                         const SourceSpan& span)
{
    const QString name = QStringLiteral("operator ") + op;
    const QList<const FunctionDeclNode*> candidates = resolveFunctionCandidates(name);
    if (candidates.isEmpty())
        return std::nullopt;

    struct Match {
        const FunctionDeclNode* fn = nullptr;
    };

    QList<Match> matches;
    int bestScore = 1'000'000;
    std::vector<PreparedCallArg> prepared(2);
    prepared[0].value = lhs;
    prepared[0].location = &lhsLocation;
    prepared[0].isReadOnly = lhsLocation.isReadOnly;
    prepared[0].span = span;
    prepared[1].value = rhs;
    prepared[1].span = span;

    for (const FunctionDeclNode* fn : candidates) {
        if (!fn || !fn->isOperator || fn->operatorSymbol != op)
            continue;
        if (fn->params.size() != 2 || (!fn->params.empty() && fn->params.back()->variadic))
            continue;
        const AbelType lhsParam = typeFromAstForDecl(*fn->params[0]->type, *fn);
        const AbelType rhsParam = typeFromAstForDecl(*fn->params[1]->type, *fn);
        const auto lhsScore = scorePreparedArgument(lhsParam, prepared[0]);
        const auto rhsScore = scorePreparedArgument(rhsParam, prepared[1]);
        if (!lhsScore || !rhsScore)
            continue;
        const int score = *lhsScore + *rhsScore;
        if (score < bestScore) {
            bestScore = score;
            matches.clear();
            matches.push_back(Match{fn});
        } else if (score == bestScore) {
            matches.push_back(Match{fn});
        }
    }

    if (matches.isEmpty())
        return std::nullopt;
    if (matches.size() > 1) {
        error(QStringLiteral("E0521"),
              QStringLiteral("operator '%1' is ambiguous for %2 and %3")
                  .arg(op, lhs.type().displayName(), rhs.type().displayName()),
              span);
        return AbelValue::makeUnknown();
    }

    return callFunctionPrepared(*matches.front().fn, prepared, span).value;
}

AbelValue Interpreter::evalCompoundAssignmentFallback(const QString& op,
                                                      const AbelValue& lhs,
                                                      const AbelValue& rhs,
                                                      const SourceSpan& span)
{
    const QString baseOp = compoundAssignmentBaseOperator(op);

    auto evalBuiltin = [&](const AbelValue& rawLhs, const AbelValue& rawRhs) -> std::optional<AbelValue> {
        if (baseOp == QStringLiteral("+") && rawLhs.type().kind == TypeKind::Str && rawRhs.type().kind == TypeKind::Str)
            return AbelValue::makeString(rawLhs.asString() + rawRhs.asString());
        if (!rawLhs.type().isNumeric() || !rawRhs.type().isNumeric())
            return std::nullopt;

        const bool useDouble = rawLhs.type().kind == TypeKind::F64 || rawRhs.type().kind == TypeKind::F64;
        if (useDouble) {
            const double a = rawLhs.asDouble();
            const double b = rawRhs.asDouble();
            if (baseOp == QStringLiteral("+")) return AbelValue::makeDouble(a + b);
            if (baseOp == QStringLiteral("-")) return AbelValue::makeDouble(a - b);
            if (baseOp == QStringLiteral("*")) return AbelValue::makeDouble(a * b);
            if (baseOp == QStringLiteral("/")) return AbelValue::makeDouble(a / b);
            if (baseOp == QStringLiteral("**")) return AbelValue::makeDouble(std::pow(a, b));
            if (baseOp == QStringLiteral("<?")) return AbelValue::makeDouble(a < b ? a : b);
            if (baseOp == QStringLiteral(">?")) return AbelValue::makeDouble(a > b ? a : b);
            return std::nullopt;
        }

        const TypeKind resultKind = numericBinaryResultKind(rawLhs.type(), rawRhs.type());
        const bool useUnsigned = rawLhs.type().isUnsignedInteger() || rawRhs.type().isUnsignedInteger();
        if (useUnsigned) {
            const quint64 a = static_cast<quint64>(rawLhs.asInt());
            const quint64 b = static_cast<quint64>(rawRhs.asInt());
            if (baseOp == QStringLiteral("+")) return AbelValue::makeInt(static_cast<qint64>(a + b), resultKind);
            if (baseOp == QStringLiteral("-")) return AbelValue::makeInt(static_cast<qint64>(a - b), resultKind);
            if (baseOp == QStringLiteral("*")) return AbelValue::makeInt(static_cast<qint64>(a * b), resultKind);
            if (baseOp == QStringLiteral("/")) {
                if (b == 0) {
                    error(QStringLiteral("E0517"), QStringLiteral("division by zero"), span);
                    return AbelValue::makeUnknown();
                }
                return AbelValue::makeInt(static_cast<qint64>(a / b), resultKind);
            }
            if (baseOp == QStringLiteral("%") || baseOp == QStringLiteral("%%")) {
                if (b == 0) {
                    error(QStringLiteral("E0518"), QStringLiteral("modulo by zero"), span);
                    return AbelValue::makeUnknown();
                }
                return AbelValue::makeInt(static_cast<qint64>(a % b), resultKind);
            }
            if (baseOp == QStringLiteral("**")) {
                quint64 out = 1;
                for (quint64 i = 0; i < b; ++i)
                    out *= a;
                return AbelValue::makeInt(static_cast<qint64>(out), resultKind);
            }
            if (baseOp == QStringLiteral("<?")) return AbelValue::makeInt(static_cast<qint64>(a < b ? a : b), resultKind);
            if (baseOp == QStringLiteral(">?")) return AbelValue::makeInt(static_cast<qint64>(a > b ? a : b), resultKind);
            return std::nullopt;
        }

        const qint64 a = rawLhs.asInt();
        const qint64 b = rawRhs.asInt();
        if (baseOp == QStringLiteral("+")) return AbelValue::makeInt(a + b, resultKind);
        if (baseOp == QStringLiteral("-")) return AbelValue::makeInt(a - b, resultKind);
        if (baseOp == QStringLiteral("*")) return AbelValue::makeInt(a * b, resultKind);
        if (baseOp == QStringLiteral("/")) {
            if (b == 0) {
                error(QStringLiteral("E0517"), QStringLiteral("division by zero"), span);
                return AbelValue::makeUnknown();
            }
            return AbelValue::makeInt(a / b, resultKind);
        }
        if (baseOp == QStringLiteral("%") || baseOp == QStringLiteral("%%")) {
            if (b == 0) {
                error(QStringLiteral("E0518"), QStringLiteral("modulo by zero"), span);
                return AbelValue::makeUnknown();
            }
            qint64 r = a % b;
            if (baseOp == QStringLiteral("%%") && r < 0)
                r += b < 0 ? -b : b;
            return AbelValue::makeInt(r, resultKind);
        }
        if (baseOp == QStringLiteral("**")) {
            if (b < 0) {
                error(QStringLiteral("E0519"), QStringLiteral("integer power with negative exponent is not supported"), span);
                return AbelValue::makeUnknown();
            }
            qint64 out = 1;
            for (qint64 i = 0; i < b; ++i)
                out *= a;
            return AbelValue::makeInt(out, resultKind);
        }
        if (baseOp == QStringLiteral("<?")) return AbelValue::makeInt(a < b ? a : b, resultKind);
        if (baseOp == QStringLiteral(">?")) return AbelValue::makeInt(a > b ? a : b, resultKind);
        return std::nullopt;
    };

    if (lhs.type().kind == TypeKind::Any || rhs.type().kind == TypeKind::Any) {
        AbelValue rawLhs = unboxAnyValue(lhs);
        AbelValue rawRhs = unboxAnyValue(rhs);
        if (auto builtin = evalBuiltin(rawLhs, rawRhs))
            return *builtin;
        if (m_ctx->hasError())
            return AbelValue::makeUnknown();
    } else if (auto builtin = evalBuiltin(lhs, rhs)) {
        return *builtin;
    }

    if (auto overloaded = evalUserBinaryOperator(baseOp, lhs, rhs, span))
        return *overloaded;

    error(QStringLiteral("E0521"),
          QStringLiteral("operator '%1' requires compatible operands").arg(op),
          span);
    return AbelValue::makeUnknown();
}

AbelValue Interpreter::evalCast(const CastExprNode& expr)
{
    AbelValue source = evalExpr(*expr.expr);
    const AbelType target = typeFromAstInCurrentPackage(*expr.targetType);

    if (source.type().kind == TypeKind::Any) {
        auto converted = dynamicCastValue(source, target, expr.span);
        return converted.value_or(AbelValue::makeUnknown());
    }

    if (!canAssignValue(target, source.type())) {
        error(QStringLiteral("E0590"),
              QStringLiteral("cannot cast %1 to %2").arg(source.type().displayName(), target.displayName()),
              expr.expr->span);
        return AbelValue::makeUnknown();
    }
    return convertValue(source, target);
}

std::optional<AbelValue> Interpreter::dynamicCastValue(const AbelValue& value,
                                                       const AbelType& target,
                                                       const SourceSpan& span,
                                                       const QString& context)
{
    const bool fromAny = value.type().kind == TypeKind::Any;
    const AbelValue source = unboxAnyValue(value);

    auto actualName = [&]() {
        QString actual = fromAny
            ? QStringLiteral("any containing %1").arg(source.type().displayName())
            : source.type().displayName();
        if (!context.isEmpty())
            actual = QStringLiteral("%1 %2").arg(context, actual);
        return actual;
    };

    if (target.kind == TypeKind::Vector && target.pointee) {
        if (source.type().kind != TypeKind::Vector) {
            error(QStringLiteral("E0591"),
                  QStringLiteral("cannot cast %1 to %2").arg(actualName(), target.displayName()),
                  span);
            return std::nullopt;
        }
        const auto vector = source.asVector();
        std::vector<AbelValue> elements;
        elements.reserve(vector->elements.size());
        for (size_t i = 0; i < vector->elements.size(); ++i) {
            auto converted = dynamicCastValue(vector->elements[i],
                                              *target.pointee,
                                              span,
                                              QStringLiteral("vector element %1").arg(i));
            if (!converted)
                return std::nullopt;
            elements.push_back(std::move(*converted));
        }
        return AbelValue::makeVector(*target.pointee, std::move(elements));
    }

    if (!canAssignValue(target, source.type())) {
        error(QStringLiteral("E0591"),
              QStringLiteral("cannot cast %1 to %2").arg(actualName(), target.displayName()),
              span);
        return std::nullopt;
    }
    return convertValue(source, target);
}

AbelValue Interpreter::evalPipe(const BinaryExprNode& expr)
{
    auto preparePipeHoleArg = [&]() {
        PreparedCallArg out;
        out.span = expr.lhs->span;
        if (exprCanHaveRuntimeLocation(*expr.lhs)) {
            if (AbelLocation* loc = evalLocation(*expr.lhs)) {
                out.location = loc;
                out.isReadOnly = loc->isReadOnly;
                out.value = loc->read();
            } else {
                out.value = AbelValue::makeUnknown();
            }
        } else {
            out.value = evalExpr(*expr.lhs);
        }
        return out;
    };

    if (dynamic_cast<DoExprNode*>(expr.rhs.get())) {
        const bool previousHasPipeHoleArg = m_hasPipeHoleArg;
        const PreparedCallArg previousPipeHoleArg = m_pipeHoleArg;
        AbelLocation* const previousPipeHoleTempLocation = m_pipeHoleTempLocation;
        m_hasPipeHoleArg = true;
        m_pipeHoleArg = preparePipeHoleArg();
        m_pipeHoleTempLocation = nullptr;
        AbelValue out = evalExpr(*expr.rhs);
        m_hasPipeHoleArg = previousHasPipeHoleArg;
        m_pipeHoleArg = previousPipeHoleArg;
        m_pipeHoleTempLocation = previousPipeHoleTempLocation;
        return out;
    }

    if (isPipeHoleReceiverExpr(*expr.rhs)) {
        if (isPipeHoleExpr(*expr.rhs)) {
            error(QStringLiteral("E0593"), QStringLiteral("pipe hole receiver must select a field or call a method"), expr.rhs->span);
            return AbelValue::makeUnknown();
        }

        const bool previousHasPipeHoleArg = m_hasPipeHoleArg;
        const PreparedCallArg previousPipeHoleArg = m_pipeHoleArg;
        AbelLocation* const previousPipeHoleTempLocation = m_pipeHoleTempLocation;
        m_hasPipeHoleArg = true;
        m_pipeHoleArg = preparePipeHoleArg();
        m_pipeHoleTempLocation = nullptr;
        AbelValue out = evalExpr(*expr.rhs);
        m_hasPipeHoleArg = previousHasPipeHoleArg;
        m_pipeHoleArg = previousPipeHoleArg;
        m_pipeHoleTempLocation = previousPipeHoleTempLocation;
        return out;
    }
    if (countPipeHoles(*expr.rhs) > 0) {
        if (auto* call = dynamic_cast<CallExprNode*>(expr.rhs.get())) {
            if (dynamic_cast<NameExprNode*>(call->callee.get()) || dynamic_cast<StaticAccessExprNode*>(call->callee.get())) {
                // Keep v1.1 structured pipe calls on the old prepared-call path.
            } else {
                const bool previousHasPipeHoleArg = m_hasPipeHoleArg;
                const PreparedCallArg previousPipeHoleArg = m_pipeHoleArg;
                AbelLocation* const previousPipeHoleTempLocation = m_pipeHoleTempLocation;
                m_hasPipeHoleArg = true;
                m_pipeHoleArg = preparePipeHoleArg();
                m_pipeHoleTempLocation = nullptr;
                AbelValue out = evalExpr(*expr.rhs);
                m_hasPipeHoleArg = previousHasPipeHoleArg;
                m_pipeHoleArg = previousPipeHoleArg;
                m_pipeHoleTempLocation = previousPipeHoleTempLocation;
                return out;
            }
        } else {
            const bool previousHasPipeHoleArg = m_hasPipeHoleArg;
            const PreparedCallArg previousPipeHoleArg = m_pipeHoleArg;
            AbelLocation* const previousPipeHoleTempLocation = m_pipeHoleTempLocation;
            m_hasPipeHoleArg = true;
            m_pipeHoleArg = preparePipeHoleArg();
            m_pipeHoleTempLocation = nullptr;
            AbelValue out = evalExpr(*expr.rhs);
            m_hasPipeHoleArg = previousHasPipeHoleArg;
            m_pipeHoleArg = previousPipeHoleArg;
            m_pipeHoleTempLocation = previousPipeHoleTempLocation;
            return out;
        }
    }

    QString targetName;
    const std::vector<std::unique_ptr<ExprNode>>* restArgs = nullptr;
    const CallExprNode* sourceCall = nullptr;
    const StaticAccessExprNode* staticTarget = nullptr;

    if (auto* name = dynamic_cast<NameExprNode*>(expr.rhs.get())) {
        targetName = name->name;
    } else if (auto* call = dynamic_cast<CallExprNode*>(expr.rhs.get())) {
        if (auto* name = dynamic_cast<NameExprNode*>(call->callee.get())) {
            targetName = name->name;
        } else if (auto* access = dynamic_cast<StaticAccessExprNode*>(call->callee.get())) {
            staticTarget = access;
        } else {
            error(QStringLiteral("E0592"),
                  QStringLiteral("pipe target call must use a named function or static/backend function"),
                  call->callee->span);
            return AbelValue::makeUnknown();
        }
        restArgs = &call->args;
        sourceCall = call;
    } else {
        error(QStringLiteral("E0593"), QStringLiteral("pipe right side must be f or f(args...)"), expr.rhs->span);
        return AbelValue::makeUnknown();
    }

    static const std::vector<std::unique_ptr<ExprNode>> emptyArgs;
    const auto& args = restArgs ? *restArgs : emptyArgs;

    auto buildPipeArgs = [&]() {
        const bool holes = hasPipeHole(args);
        PreparedCallArg lhs = preparePipeHoleArg();
        std::vector<PreparedCallArg> prepared;
        std::vector<bool> holeFlags;
        prepared.reserve(args.size() + (holes ? 0 : 1));
        holeFlags.reserve(args.size() + (holes ? 0 : 1));
        if (!holes) {
            prepared.push_back(lhs);
            holeFlags.push_back(false);
        }
        for (const auto& arg : args) {
            if (isPipeHoleExpr(*arg)) {
                PreparedCallArg hole = lhs;
                hole.span = arg->span;
                prepared.push_back(hole);
                holeFlags.push_back(true);
            } else {
                PreparedCallArg out;
                out.span = arg->span;
                if (exprCanHaveRuntimeLocation(*arg)) {
                    if (AbelLocation* loc = evalLocation(*arg)) {
                        out.location = loc;
                        out.isReadOnly = loc->isReadOnly;
                        out.value = loc->read();
                    } else {
                        out.value = AbelValue::makeUnknown();
                    }
                } else {
                    out.value = evalExpr(*arg);
                }
                prepared.push_back(std::move(out));
                holeFlags.push_back(false);
            }
        }
        return std::pair<std::vector<PreparedCallArg>, std::vector<bool>>(std::move(prepared), std::move(holeFlags));
    };

    auto makeSyntheticPipeCall = [&](const ExprNode* callee, const std::vector<PreparedCallArg>& prepared) {
        CallExprNode call;
        call.span = expr.span;
        if (callee) {
            call.callee = cloneCallableExprNode(*callee);
        } else {
            auto dummy = std::make_unique<NameExprNode>();
            dummy->name = targetName;
            dummy->span = expr.rhs->span;
            call.callee = std::move(dummy);
        }
        call.args.reserve(prepared.size());
        call.argNames.reserve(prepared.size());
        call.argSpreads.reserve(prepared.size());
        const bool holes = hasPipeHole(args);
        if (!holes) {
            auto dummy = std::make_unique<NameExprNode>();
            dummy->span = expr.lhs->span;
            call.args.push_back(std::move(dummy));
            call.argNames.push_back({});
            call.argSpreads.push_back(false);
        }
        for (size_t i = 0; i < args.size(); ++i) {
            auto dummy = std::make_unique<NameExprNode>();
            dummy->span = args[i]->span;
            call.args.push_back(std::move(dummy));
            call.argNames.push_back(sourceCall ? callArgName(*sourceCall, i) : QString());
            call.argSpreads.push_back(sourceCall && callArgSpread(*sourceCall, i));
        }
        return call;
    };

    if (staticTarget) {
        auto [prepared, holeFlags] = buildPipeArgs();
        if (m_ctx->hasError())
            return AbelValue::makeUnknown();
        CallExprNode call = makeSyntheticPipeCall(sourceCall ? sourceCall->callee.get() : staticTarget, prepared);
        return evalStaticCall(*staticTarget, call, &prepared, &holeFlags);
    }

    if (const VariableSlot* slot = m_ctx->lookupVariable(targetName)) {
        if (sourceCall && callHasStructuredArgs(*sourceCall)) {
            error(QStringLiteral("E0586"), QStringLiteral("function value calls only accept positional arguments"), expr.rhs->span);
            return AbelValue::makeUnknown();
        }
        AbelValue callee = slot->location ? slot->location->read() : AbelValue::makeUnknown();
        if (callee.type().kind == TypeKind::Function)
            return callFunctionValuePipe(callee, *expr.lhs, args, expr.span);
        error(QStringLiteral("E0594"), QStringLiteral("pipe target variable '%1' is not a function value").arg(targetName), expr.rhs->span);
        return AbelValue::makeUnknown();
    }

    if (m_functions.contains(targetName)) {
        const QList<const FunctionDeclNode*> candidates = resolveFunctionCandidates(targetName);
        if (!candidates.isEmpty()) {
            auto [prepared, holeFlags] = buildPipeArgs();
            if (m_ctx->hasError())
                return AbelValue::makeUnknown();
            CallExprNode call = makeSyntheticPipeCall(sourceCall ? sourceCall->callee.get() : nullptr, prepared);
            return callStructuredFunctionOverloadPrepared(targetName, candidates, call, prepared, &holeFlags).value;
        }
    }

    if (sourceCall) {
        if (const TypeAliasDeclNode* alias = resolveTypeAlias(targetName, m_currentPackage)) {
            AbelType aliased = typeFromAstForDecl(*alias->targetType, *alias);
            if (aliased.kind != TypeKind::Struct) {
                error(QStringLiteral("E0544"),
                      QStringLiteral("type alias '%1' does not name a constructible struct").arg(targetName),
                      expr.span);
                return AbelValue::makeUnknown();
            }
            const StructRuntimeInfo* info = structInfoForType(aliased);
            if (!info) {
                error(QStringLiteral("E0544"), QStringLiteral("unknown struct type '%1'").arg(aliased.displayName()), expr.span);
                return AbelValue::makeUnknown();
            }
            auto [prepared, holeFlags] = buildPipeArgs();
            if (m_ctx->hasError())
                return AbelValue::makeUnknown();
            CallExprNode call = makeSyntheticPipeCall(sourceCall->callee.get(), prepared);
            return evalStructConstructor(targetName, *info, call, &aliased, &prepared, &holeFlags);
        }

        if (const StructRuntimeInfo* info = resolveStruct(targetName)) {
            auto [prepared, holeFlags] = buildPipeArgs();
            if (m_ctx->hasError())
                return AbelValue::makeUnknown();
            CallExprNode call = makeSyntheticPipeCall(sourceCall->callee.get(), prepared);
            return evalStructConstructor(targetName, *info, call, nullptr, &prepared, &holeFlags);
        }
    }

    if (m_builtins.hasFunction(targetName)) {
        if (sourceCall && callHasNamedArgs(*sourceCall)) {
            error(QStringLiteral("E0544"), QStringLiteral("builtin function calls do not support named arguments"), expr.rhs->span);
            return AbelValue::makeUnknown();
        }
        const bool spreadBuiltin = targetName == QStringLiteral("build_string")
            || targetName == QStringLiteral("print")
            || targetName == QStringLiteral("println");
        if (sourceCall && callHasSpreadArgs(*sourceCall) && !spreadBuiltin) {
            error(QStringLiteral("E0544"), QStringLiteral("spread arguments are only supported for any... builtin functions"), expr.rhs->span);
            return AbelValue::makeUnknown();
        }
        BuiltinFunctionCall call{*m_ctx, targetName, {}, {}, expr.span};
        attachStringifier(call);
        const bool holes = hasPipeHole(args);
        call.args.reserve(args.size() + (holes ? 0 : 1));
        call.argSpans.reserve(args.size() + (holes ? 0 : 1));
        AbelValue lhsValue = evalExpr(*expr.lhs);
        auto appendArg = [&](const AbelValue& value, const SourceSpan& argSpan, bool spread) {
            if (spread) {
                if (!isVectorAnyType(value.type())) {
                    error(QStringLiteral("E0544"),
                          QStringLiteral("spread argument expects vector<any>, got %1").arg(value.type().displayName()),
                          argSpan);
                    return;
                }
                auto vector = value.asVector();
                for (const AbelValue& element : vector->elements) {
                    call.args.push_back(element.isBoxedAny() ? element.asAny()->value : element);
                    call.argSpans.push_back(argSpan);
                }
            } else {
                call.args.push_back(value);
                call.argSpans.push_back(argSpan);
            }
        };
        if (!holes)
            appendArg(lhsValue, expr.lhs->span, false);
        for (size_t i = 0; i < args.size(); ++i) {
            const auto& arg = args[i];
            const bool spread = sourceCall && callArgSpread(*sourceCall, i);
            if (isPipeHoleExpr(*arg))
                appendArg(lhsValue, arg->span, spread);
            else
                appendArg(evalExpr(*arg), arg->span, spread);
        }
        if (m_ctx->hasError())
            return AbelValue::makeUnknown();
        return m_builtins.callFunction(std::move(call));
    }

    error(QStringLiteral("E0595"), QStringLiteral("unknown pipe target '%1'").arg(targetName), expr.rhs->span);
    return AbelValue::makeUnknown();
}

AbelValue Interpreter::evalAnyTupleLiteral(const AnyTupleLiteralExprNode& expr)
{
    auto elements = std::make_shared<std::vector<AbelValue>>();
    elements->reserve(expr.values.size());
    for (const auto& value : expr.values) {
        AbelValue evaluated = evalExpr(*value);
        if (evaluated.type().kind == TypeKind::Unknown)
            return AbelValue::makeUnknown();
        elements->push_back(unboxAnyValue(evaluated));
    }

    auto object = std::make_shared<AbelDynamicObject>();
    object->kind = QStringLiteral("tuple");
    object->get = [elements](const AbelValue& key, AbelRuntimeContext& ctx, const SourceSpan& span) {
        AbelValue rawKey = unboxAny(key);
        if (!rawKey.type().isInteger()) {
            ctx.error(QStringLiteral("E0642"),
                      QStringLiteral("tuple index must be integer, got %1").arg(rawKey.type().displayName()),
                      span);
            return AbelValue::makeUnknown();
        }
        const qint64 index = rawKey.asInt();
        if (index < 0 || static_cast<size_t>(index) >= elements->size()) {
            ctx.error(QStringLiteral("E0643"),
                      QStringLiteral("tuple index %1 out of range for length %2")
                          .arg(index)
                          .arg(elements->size()),
                      span);
            return AbelValue::makeUnknown();
        }
        return AbelValue::makeAny((*elements)[static_cast<size_t>(index)]);
    };
    object->set = [elements](const AbelValue& key, const AbelValue& value, AbelRuntimeContext& ctx, const SourceSpan& span) {
        AbelValue rawKey = unboxAny(key);
        if (!rawKey.type().isInteger()) {
            ctx.error(QStringLiteral("E0642"),
                      QStringLiteral("tuple index must be integer, got %1").arg(rawKey.type().displayName()),
                      span);
            return;
        }
        const qint64 index = rawKey.asInt();
        if (index < 0 || static_cast<size_t>(index) >= elements->size()) {
            ctx.error(QStringLiteral("E0643"),
                      QStringLiteral("tuple index %1 out of range for length %2")
                          .arg(index)
                          .arg(elements->size()),
                      span);
            return;
        }
        (*elements)[static_cast<size_t>(index)] = unboxAny(value);
    };
    object->equals = [object](const AbelValue& other, AbelRuntimeContext&, const SourceSpan&) -> std::optional<bool> {
        return other.isDynamicObject() && other.asDynamicObject() == object;
    };
    object->debug = [elements] {
        return QStringLiteral("<tuple len=%1>").arg(elements->size());
    };
    return AbelValue::makeDynamicObject(object);
}

std::optional<AbelValue> Interpreter::evalDynamicIndex(const AbelValue& base, qint64 index, const SourceSpan& span)
{
    const AbelValue rawBase = unboxAnyValue(base);
    if (!rawBase.isDynamicObject()) {
        error(QStringLiteral("E0644"),
              QStringLiteral("tuple cast source must be dynamic object, got %1")
                  .arg(rawBase.type().displayName()),
              span);
        return std::nullopt;
    }
    auto object = rawBase.asDynamicObject();
    if (!object || !object->get) {
        error(QStringLiteral("E0644"),
              QStringLiteral("dynamic object '%1' does not support tuple cast indexing")
                  .arg(object ? object->kind : QStringLiteral("null")),
              span);
        return std::nullopt;
    }
    AbelValue value = object->get(AbelValue::makeInt(index, TypeKind::I32), *m_ctx, span);
    if (value.type().kind == TypeKind::Unknown || m_ctx->hasError())
        return std::nullopt;
    return value;
}

AbelValue Interpreter::evalStrMapLiteral(const StrMapLiteralExprNode& expr)
{
    auto values = std::make_shared<QHash<QString, AbelValue>>();
    for (const auto& entry : expr.entries) {
        AbelValue evaluated = evalExpr(*entry.value);
        if (evaluated.type().kind == TypeKind::Unknown)
            return AbelValue::makeUnknown();
        values->insert(entry.key, unboxAnyValue(evaluated));
    }

    auto object = std::make_shared<AbelDynamicObject>();
    object->kind = QStringLiteral("strmap");
    object->get = [values](const AbelValue& key, AbelRuntimeContext& ctx, const SourceSpan& span) {
        AbelValue rawKey = unboxAny(key);
        if (rawKey.type().kind != TypeKind::Str) {
            ctx.error(QStringLiteral("E0644"),
                      QStringLiteral("strmap key must be str, got %1").arg(rawKey.type().displayName()),
                      span);
            return AbelValue::makeUnknown();
        }
        auto found = values->constFind(rawKey.asString());
        if (found == values->constEnd()) {
            ctx.error(QStringLiteral("E0645"),
                      QStringLiteral("strmap missing key '%1'").arg(rawKey.asString()),
                      span);
            return AbelValue::makeUnknown();
        }
        return AbelValue::makeAny(found.value());
    };
    object->set = [values](const AbelValue& key, const AbelValue& value, AbelRuntimeContext& ctx, const SourceSpan& span) {
        AbelValue rawKey = unboxAny(key);
        if (rawKey.type().kind != TypeKind::Str) {
            ctx.error(QStringLiteral("E0644"),
                      QStringLiteral("strmap key must be str, got %1").arg(rawKey.type().displayName()),
                      span);
            return;
        }
        values->insert(rawKey.asString(), unboxAny(value));
    };
    object->equals = [object](const AbelValue& other, AbelRuntimeContext&, const SourceSpan&) -> std::optional<bool> {
        return other.isDynamicObject() && other.asDynamicObject() == object;
    };
    object->debug = [values] {
        return QStringLiteral("<strmap len=%1>").arg(values->size());
    };
    return AbelValue::makeDynamicObject(object);
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
    if (auto* access = dynamic_cast<StaticAccessExprNode*>(expr.callee.get())) {
        return evalStaticCall(*access, expr);
    }

    if (auto* field = dynamic_cast<FieldAccessExprNode*>(expr.callee.get())) {
        const bool inPipeHoleReceiver = m_hasPipeHoleArg && isPipeHoleReceiverExpr(*field->base);
        std::vector<bool> rawPipeHoles;
        const std::vector<bool>* rawPipeHolesPtr = nullptr;
        int totalPipeHoles = 0;
        if (inPipeHoleReceiver) {
            rawPipeHoles.reserve(expr.args.size());
            for (const auto& arg : expr.args)
                rawPipeHoles.push_back(isPipeHoleExpr(*arg));
            rawPipeHolesPtr = &rawPipeHoles;
            totalPipeHoles = countPipeHoles(expr);
        }
        AbelValue receiver = evalExpr(*field->base);
        if (receiver.type().kind == TypeKind::Struct)
            return evalStructMethod(*field, expr, inPipeHoleReceiver, rawPipeHolesPtr, totalPipeHoles);
        if (callHasStructuredArgs(expr)) {
            error(QStringLiteral("E0524"), QStringLiteral("builtin method calls do not support named, default, or spread arguments"), expr.span);
            return AbelValue::makeUnknown();
        }
        return evalBuiltinMethod(*field, expr.args, inPipeHoleReceiver, totalPipeHoles);
    }

    auto* name = dynamic_cast<NameExprNode*>(expr.callee.get());
    if (!name) {
        if (callHasStructuredArgs(expr)) {
            error(QStringLiteral("E0524"), QStringLiteral("function value calls only accept positional arguments"), expr.span);
            return AbelValue::makeUnknown();
        }
        AbelValue callee = evalExpr(*expr.callee);
        if (callee.type().kind == TypeKind::Function)
            return callFunctionValue(callee, expr.args, expr.span);
        error(QStringLiteral("E0524"), QStringLiteral("callee is not a function value"), expr.span);
        return AbelValue::makeUnknown();
    }
    if (const VariableSlot* slot = m_ctx->lookupVariable(name->name)) {
        if (callHasStructuredArgs(expr)) {
            error(QStringLiteral("E0586"), QStringLiteral("function value calls only accept positional arguments"), expr.span);
            return AbelValue::makeUnknown();
        }
        AbelValue callee = slot->location ? slot->location->read() : AbelValue::makeUnknown();
        if (callee.type().kind == TypeKind::Function)
            return callFunctionValue(callee, expr.args, expr.span);
        error(QStringLiteral("E0586"), QStringLiteral("variable '%1' is not a function value").arg(name->name), expr.span);
        return AbelValue::makeUnknown();
    }
    if (const TypeAliasDeclNode* alias = resolveTypeAlias(name->name, m_currentPackage)) {
        AbelType aliased = typeFromAstForDecl(*alias->targetType, *alias);
        if (aliased.kind != TypeKind::Struct) {
            error(QStringLiteral("E0544"),
                  QStringLiteral("type alias '%1' does not name a constructible struct").arg(name->name),
                  expr.span);
            return AbelValue::makeUnknown();
        }
        const StructRuntimeInfo* info = structInfoForType(aliased);
        if (!info) {
            error(QStringLiteral("E0544"), QStringLiteral("unknown struct type '%1'").arg(aliased.displayName()), expr.span);
            return AbelValue::makeUnknown();
        }
        return evalStructConstructor(name->name, *info, expr, &aliased);
    }
    if (const StructRuntimeInfo* info = resolveStruct(name->name))
    {
        return evalStructConstructor(name->name, *info, expr);
    }

    if (m_functions.contains(name->name)) {
        const QList<const FunctionDeclNode*> candidates = resolveFunctionCandidates(name->name);
        if (!candidates.isEmpty())
            return callStructuredFunctionOverloadExpr(name->name, candidates, expr).value;
    }

    if (m_builtins.hasFunction(name->name)) {
        if (callHasNamedArgs(expr)) {
            error(QStringLiteral("E0544"), QStringLiteral("builtin function calls do not support named arguments"), expr.span);
            return AbelValue::makeUnknown();
        }
        const bool spreadBuiltin = name->name == QStringLiteral("build_string")
            || name->name == QStringLiteral("print")
            || name->name == QStringLiteral("println");
        if (callHasSpreadArgs(expr) && !spreadBuiltin) {
            error(QStringLiteral("E0544"), QStringLiteral("spread arguments are only supported for any... builtin functions"), expr.span);
            return AbelValue::makeUnknown();
        }
        BuiltinFunctionCall call{*m_ctx, name->name, {}, {}, expr.span};
        attachStringifier(call);
        call.args.reserve(expr.args.size());
        call.argSpans.reserve(expr.args.size());
        for (size_t i = 0; i < expr.args.size(); ++i) {
            const auto& arg = expr.args[i];
            AbelValue value = evalExpr(*arg);
            if (callArgSpread(expr, i)) {
                if (!isVectorAnyType(value.type())) {
                    error(QStringLiteral("E0544"),
                          QStringLiteral("spread argument expects vector<any>, got %1").arg(value.type().displayName()),
                          arg->span);
                    return AbelValue::makeUnknown();
                }
                auto vector = value.asVector();
                for (const AbelValue& element : vector->elements) {
                    call.args.push_back(element.isBoxedAny() ? element.asAny()->value : element);
                    call.argSpans.push_back(arg->span);
                }
            } else {
                call.args.push_back(value);
                call.argSpans.push_back(arg->span);
            }
        }
        return m_builtins.callFunction(std::move(call));
    }

    error(QStringLiteral("E0525"), QStringLiteral("unknown function '%1'").arg(name->name), expr.span);
    return AbelValue::makeUnknown();
}

AbelValue Interpreter::evalStaticCall(const StaticAccessExprNode& callee,
                                      const CallExprNode& call,
                                      const std::vector<PreparedCallArg>* preparedArgs,
                                      const std::vector<bool>* rawPipeHoles)
{
    if (auto* nested = dynamic_cast<StaticAccessExprNode*>(callee.base.get())) {
        const QString moduleName = staticAccessModuleName(*nested);
        const QString resolvedModuleName = resolveModuleName(moduleName);
        const QString backendName = nested->member;
        if (!moduleName.isEmpty()) {
            const auto backends = m_backends.value(backendName);
            for (const auto& info : backends) {
                if (info.decl && info.decl->moduleName == resolvedModuleName)
                    return evalBackendCallByName(moduleName + QStringLiteral("::") + backendName,
                                                 nested->span,
                                                 callee.member,
                                                 call,
                                                 preparedArgs,
                                                 rawPipeHoles);
            }
        }
    }

    const QString baseName = staticAccessName(*callee.base);
    if (baseName.isEmpty()) {
        error(QStringLiteral("E0603"), QStringLiteral("static/backend call receiver must be a name"), callee.span);
        return AbelValue::makeUnknown();
    }

    QString moduleName = baseName;
    moduleName.replace(QStringLiteral("::"), QStringLiteral("."));
    const QString resolvedModuleName = resolveModuleName(moduleName);

    if (resolveStructInModule(moduleName, callee.member))
        return evalQualifiedStructConstructor(moduleName, callee.member, call, preparedArgs, rawPipeHoles);

    const auto functions = m_functions.value(callee.member);
    for (const FunctionDeclNode* fn : functions) {
        if (fn->moduleName == resolvedModuleName)
            return evalQualifiedFunctionCall(moduleName, callee.member, call, preparedArgs, rawPipeHoles);
    }

    return evalBackendCallByName(baseName, callee.base->span, callee.member, call, preparedArgs, rawPipeHoles);
}

AbelValue Interpreter::evalBackendCall(const StaticAccessExprNode& callee,
                                       const CallExprNode& call,
                                       const std::vector<PreparedCallArg>* preparedArgs,
                                       const std::vector<bool>* rawPipeHoles)
{
    auto* backendName = dynamic_cast<NameExprNode*>(callee.base.get());
    if (!backendName) {
        error(QStringLiteral("E0603"), QStringLiteral("backend call receiver must be a backend name"), callee.span);
        return AbelValue::makeUnknown();
    }
    return evalBackendCallByName(backendName->name, backendName->span, callee.member, call, preparedArgs, rawPipeHoles);
}

AbelValue Interpreter::evalBackendCallByName(const QString& backendName,
                                             const SourceSpan& backendSpan,
                                             const QString& member,
                                             const CallExprNode& call,
                                             const std::vector<PreparedCallArg>* preparedArgs,
                                             const std::vector<bool>* rawPipeHoles)
{
    Q_ASSERT(!preparedArgs || preparedArgs->size() == call.args.size());
    Q_ASSERT(!rawPipeHoles || rawPipeHoles->size() == call.args.size());
    const int qualifiedSep = backendName.lastIndexOf(QStringLiteral("::"));
    const QString simpleBackendName = qualifiedSep >= 0 ? backendName.mid(qualifiedSep + 2) : backendName;
    QString moduleName;
    const BackendRuntimeInfo* backend = nullptr;
    if (qualifiedSep >= 0) {
        moduleName = backendName.left(qualifiedSep);
        moduleName.replace(QStringLiteral("::"), QStringLiteral("."));
        backend = resolveBackendInModule(moduleName, simpleBackendName);
    } else {
        backend = resolveBackend(simpleBackendName);
    }
    if (!backend || !backend->decl) {
        error(QStringLiteral("E0604"), QStringLiteral("unknown backend '%1'").arg(backendName), backendSpan);
        return AbelValue::makeUnknown();
    }
    DeclContextGuard context(m_currentPackage, m_currentModule, m_currentImports, m_currentImportAliases, *backend->decl);
    const FunctionDeclNode* fn = backend->functions.value(member, nullptr);
    if (!fn) {
        error(QStringLiteral("E0605"), QStringLiteral("unknown backend function '%1::%2'").arg(backendName, member), backendSpan);
        return AbelValue::makeUnknown();
    }
    std::vector<PreparedCallArg> rawArgs;
    if (preparedArgs) {
        rawArgs = *preparedArgs;
    } else {
        rawArgs = prepareFunctionArgs(call.args);
    }
    if (m_ctx->hasError())
        return AbelValue::makeUnknown();
    NormalizedPreparedCallArgs normalized = normalizePreparedCallArgsForParams(*fn,
                                                                               backendName + QStringLiteral("::") + member,
                                                                               fn->params,
                                                                               call,
                                                                               rawArgs,
                                                                               true,
                                                                               true,
                                                                               rawPipeHoles);
    if (!normalized.ok || m_ctx->hasError())
        return AbelValue::makeUnknown();

    const bool variadic = !fn->params.empty() && fn->params.back()->variadic;
    const size_t fixedCount = variadic ? fn->params.size() - 1 : fn->params.size();

    std::vector<AbelValue> values;
    std::vector<AbelLocation*> locations;
    values.reserve(normalized.args.size());
    locations.reserve(normalized.args.size());
    int mutableRefHoleCount = 0;
    for (size_t i = 0; i < fixedCount; ++i) {
        const ParameterNode& param = *fn->params[i];
        const AbelType target = typeFromAstInCurrentPackage(*param.type);
        const PreparedCallArg& arg = normalized.args[i];
        if (target.isReference()) {
            if (i < normalized.pipeHoles.size()
                && normalized.pipeHoles[i]
                && !(target.pointee && target.pointee->isConst)) {
                ++mutableRefHoleCount;
            }
            AbelLocation* loc = arg.location;
            if (!loc) {
                error(QStringLiteral("E0609"), QStringLiteral("backend parameter '%1' requires lvalue").arg(param.name), arg.span);
                values.push_back(AbelValue::makeUnknown());
                locations.push_back(nullptr);
                continue;
            }
            const bool paramConst = isReadOnlyBinding(target, param.type->isConst);
            if (!paramConst && loc->isReadOnly) {
                error(QStringLiteral("E0609"),
                      QStringLiteral("non-const backend parameter '%1' cannot bind to const lvalue").arg(param.name),
                      arg.span);
                values.push_back(AbelValue::makeUnknown());
                locations.push_back(nullptr);
                continue;
            }
            AbelValue current = loc->read();
            if (!canBindReferenceValue(target, current.type())) {
                error(QStringLiteral("E0609"),
                      QStringLiteral("cannot bind backend parameter '%1' of type %2 to %3 lvalue")
                          .arg(param.name, target.displayName(), current.type().displayName()),
                      arg.span);
                values.push_back(AbelValue::makeUnknown());
                locations.push_back(nullptr);
                continue;
            }
            values.push_back(convertOrError(current, *target.pointee, arg.span));
            locations.push_back(loc);
        } else {
            values.push_back(convertOrError(arg.value, target, arg.span));
            locations.push_back(nullptr);
        }
    }
    if (mutableRefHoleCount > 1)
        error(QStringLiteral("E0609"),
              QStringLiteral("pipe RHS cannot bind the same hole to multiple mutable reference parameters"),
              call.span);
    if (variadic) {
        for (size_t i = fixedCount; i < normalized.args.size(); ++i) {
            values.push_back(AbelValue::makeAny(normalized.args[i].value));
            locations.push_back(nullptr);
        }
    }
    if (m_ctx->hasError())
        return AbelValue::makeUnknown();

    QList<Diagnostic> diagnostics;
    RuntimeFrameGuard frame(*m_ctx, true, backendFrameSymbol(backendName, member), call.span);
    AbelValue result = m_activeBackendRegistry->call({simpleBackendName, member, std::move(values), call.span, std::move(locations)}, diagnostics, m_ctx);
    for (const auto& diagnostic : diagnostics)
        m_ctx->error(diagnostic.code, diagnostic.message, diagnostic.primary);
    return result;
}

AbelValue Interpreter::evalQualifiedFunctionCall(const QString& moduleName,
                                                 const QString& name,
                                                 const CallExprNode& call,
                                                 const std::vector<PreparedCallArg>* preparedArgs,
                                                 const std::vector<bool>* rawPipeHoles)
{
    const QList<const FunctionDeclNode*> candidates = resolveFunctionCandidatesInModule(moduleName, name);
    if (candidates.isEmpty()) {
        error(QStringLiteral("E0525"), QStringLiteral("unknown function '%1::%2'").arg(moduleName, name), call.span);
        return AbelValue::makeUnknown();
    }
    if (preparedArgs)
        return callStructuredFunctionOverloadPrepared(moduleName + QStringLiteral("::") + name,
                                                      candidates,
                                                      call,
                                                      *preparedArgs,
                                                      rawPipeHoles)
            .value;
    return callStructuredFunctionOverloadExpr(moduleName + QStringLiteral("::") + name, candidates, call).value;
}

AbelValue Interpreter::evalQualifiedStructConstructor(const QString& moduleName,
                                                      const QString& name,
                                                      const CallExprNode& call,
                                                      const std::vector<PreparedCallArg>* preparedArgs,
                                                      const std::vector<bool>* rawPipeHoles)
{
    const StructRuntimeInfo* info = resolveStructInModule(moduleName, name);
    if (!info) {
        error(QStringLiteral("E0580"), QStringLiteral("unknown struct '%1::%2'").arg(moduleName, name), call.span);
        return AbelValue::makeUnknown();
    }
    return evalStructConstructor(moduleName + QStringLiteral("::") + name, *info, call, nullptr, preparedArgs, rawPipeHoles);
}

AbelValue Interpreter::evalDoExpression(const DoExprNode& expr)
{
    m_ctx->pushFrame();
    ExecResult flow = execBlock(*expr.ownedBody);
    m_ctx->popFrame();
    if (m_ctx->hasError())
        return AbelValue::makeUnknown();
    if (flow.kind == FlowKind::Return)
        return flow.value;
    if (flow.kind == FlowKind::Break || flow.kind == FlowKind::Continue) {
        error(QStringLiteral("E0595"), QStringLiteral("break/continue cannot leave do expression"), expr.span);
        return AbelValue::makeUnknown();
    }
    return AbelValue::makeVoid();
}

AbelValue Interpreter::evalLambda(const LambdaExprNode& expr)
{
    AbelType returnType = typeFromAstInCurrentPackage(*expr.returnType);
    std::vector<AbelType> params;
    params.reserve(expr.paramTypes.size());
    for (const auto& param : expr.paramTypes)
        params.push_back(typeFromAstInCurrentPackage(*param));

    auto function = std::make_shared<AbelFunctionValue>();
    function->lambda = &expr;
    function->packageName = m_currentPackage;
    function->moduleName = m_currentModule;
    function->currentStruct = m_currentStruct;
    function->importedModules = m_currentImports;
    function->importedModuleAliases = m_currentImportAliases;

    enum class CaptureMode {
        None,
        Value,
        Reference,
    };
    CaptureMode defaultMode = CaptureMode::None;
    QHash<QString, CaptureMode> explicitCaptures;
    const QStringList captures = expr.captureText.split(QStringLiteral(","), Qt::SkipEmptyParts);
    for (const QString& raw : captures) {
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

    auto visible = m_ctx->visibleVariables();
    QSet<QString> capturedNames;
    auto captureOne = [&](const QString& name, CaptureMode mode) {
        if (capturedNames.contains(name))
            return;
        const VariableSlot slot = visible.value(name, VariableSlot{});
        if (!slot.location) {
            error(QStringLiteral("E0587"), QStringLiteral("unknown capture '%1'").arg(name), expr.span);
            return;
        }
        capturedNames.insert(name);
        if (mode == CaptureMode::Reference) {
            function->refCaptures.insert(name, slot.location);
            function->refConstness.insert(name, slot.isConst);
        } else if (mode == CaptureMode::Value) {
            AbelValue value = slot.location->read();
            function->valueCaptures.insert(name, convertValue(value, value.type()));
        }
    };

    if (defaultMode != CaptureMode::None) {
        for (auto it = visible.constBegin(); it != visible.constEnd(); ++it) {
            if (!explicitCaptures.contains(it.key()))
                captureOne(it.key(), defaultMode);
        }
    }
    for (auto it = explicitCaptures.constBegin(); it != explicitCaptures.constEnd(); ++it)
        captureOne(it.key(), it.value());

    if (m_ctx->hasError())
        return AbelValue::makeUnknown();
    AbelType type = makeFunctionType(returnType, std::move(params));
    AbelValue value = AbelValue::makeFunction(type, function);
    std::weak_ptr<AbelFunctionValue> weakFunction = function;
    function->invoke = [this, type, weakFunction](const std::vector<AbelValue>& args, AbelRuntimeContext& ctx, const SourceSpan& span) {
        auto locked = weakFunction.lock();
        if (!locked) {
            ctx.error(QStringLiteral("E0581"), QStringLiteral("invalid function value"), span);
            return AbelValue::makeUnknown();
        }
        return invokeFunctionValueRaw(AbelValue::makeFunction(type, locked), args, ctx, span);
    };
    return value;
}

AbelValue Interpreter::makeFunctionValue(const FunctionDeclNode& fn)
{
    std::vector<AbelType> params;
    params.reserve(fn.params.size());
    for (const auto& param : fn.params)
        params.push_back(typeFromAstForDecl(*param->type, fn));

    auto function = std::make_shared<AbelFunctionValue>();
    function->function = &fn;
    function->packageName = fn.packageName;
    function->moduleName = fn.moduleName;
    function->currentStruct.clear();
    function->importedModules = fn.importedModules;
    function->importedModuleAliases = fn.importedModuleAliases;
    AbelType type = makeFunctionType(typeFromAstForDecl(*fn.returnType, fn), std::move(params));
    AbelValue value = AbelValue::makeFunction(type, function);
    std::weak_ptr<AbelFunctionValue> weakFunction = function;
    function->invoke = [this, type, weakFunction](const std::vector<AbelValue>& args, AbelRuntimeContext& ctx, const SourceSpan& span) {
        auto locked = weakFunction.lock();
        if (!locked) {
            ctx.error(QStringLiteral("E0581"), QStringLiteral("invalid function value"), span);
            return AbelValue::makeUnknown();
        }
        return invokeFunctionValueRaw(AbelValue::makeFunction(type, locked), args, ctx, span);
    };
    return value;
}

AbelValue Interpreter::evalStructConstructor(const QString& name,
                                             const StructRuntimeInfo& info,
                                             const CallExprNode& call,
                                             const AbelType* constructedType,
                                             const std::vector<PreparedCallArg>* preparedArgs,
                                             const std::vector<bool>* rawPipeHoles)
{
    Q_ASSERT(!preparedArgs || preparedArgs->size() == call.args.size());
    Q_ASSERT(!rawPipeHoles || rawPipeHoles->size() == call.args.size());
    DeclContextGuard context(m_currentPackage, m_currentModule, m_currentImports, m_currentImportAliases, *info.decl);
    const AbelType resultType = constructedType ? *constructedType : makeStructType(structTypeName(*info.decl));

    if (info.constructors.isEmpty()) {
        if (callHasStructuredArgs(call)) {
            error(QStringLiteral("E0575"), QStringLiteral("structured constructor arguments require an explicit init declaration"), call.span);
            return AbelValue::makeUnknown();
        }
        if (call.args.empty())
            return defaultConstructValue(resultType, call.span);
        if (call.args.size() != info.decl->fields.size()) {
            error(QStringLiteral("E0575"), QStringLiteral("constructor '%1' expects %2 argument(s)").arg(name).arg(info.decl->fields.size()), call.span);
            return AbelValue::makeUnknown();
        }
        QHash<QString, AbelValue> fields;
        std::vector<QString> order;
        order.reserve(info.decl->fields.size());
        for (size_t i = 0; i < call.args.size(); ++i) {
            const auto& field = info.decl->fields[i];
            if (field->isPrivate && !isCurrentStruct(info)) {
                error(QStringLiteral("E0575"), QStringLiteral("field '%1' is private").arg(field->name), call.args[i]->span);
                continue;
            }
            order.push_back(field->name);
            const AbelValue raw = preparedArgs ? (*preparedArgs)[i].value : evalExpr(*call.args[i]);
            AbelValue value = convertOrError(raw, typeFromAstInCurrentPackage(*field->type), call.args[i]->span);
            fields.insert(field->name, value);
        }
        if (m_ctx->hasError())
            return AbelValue::makeUnknown();
        return AbelValue::makeStruct(resultType.spelling, order, std::move(fields));
    }

    std::vector<PreparedCallArg> rawArgs;
    if (preparedArgs) {
        rawArgs = *preparedArgs;
    } else {
        rawArgs = prepareFunctionArgs(call.args);
    }
    if (m_ctx->hasError())
        return AbelValue::makeUnknown();

    auto rejectMultipleMutableRefHoles = [&](const ConstructorDeclNode& ctor, const NormalizedPreparedCallArgs& normalized) -> bool {
        const bool variadic = !ctor.params.empty() && ctor.params.back()->variadic;
        const size_t fixedCount = variadic ? ctor.params.size() - 1 : ctor.params.size();
        int mutableRefHoleCount = 0;
        for (size_t i = 0; i < fixedCount; ++i) {
            const AbelType paramType = typeFromAstForDecl(*ctor.params[i]->type, *info.decl);
            if (i < normalized.pipeHoles.size()
                && normalized.pipeHoles[i]
                && paramType.isReference()
                && !(paramType.pointee && paramType.pointee->isConst)) {
                ++mutableRefHoleCount;
            }
        }
        if (mutableRefHoleCount > 1) {
            error(QStringLiteral("E0576"),
                  QStringLiteral("pipe RHS cannot bind the same hole to multiple mutable reference parameters"),
                  call.span);
            return true;
        }
        return false;
    };

    if (info.constructors.size() == 1) {
        const ConstructorDeclNode* ctor = info.constructors.front();
        if (!ctor)
            return AbelValue::makeUnknown();
        NormalizedPreparedCallArgs normalized = normalizePreparedCallArgsForParams(*info.decl,
                                                                                   name,
                                                                                   ctor->params,
                                                                                   call,
                                                                                   rawArgs,
                                                                                   true,
                                                                                   true,
                                                                                   rawPipeHoles);
        if (!normalized.ok || m_ctx->hasError())
            return AbelValue::makeUnknown();
        if (rejectMultipleMutableRefHoles(*ctor, normalized))
            return AbelValue::makeUnknown();
        return evalStructConstructorPrepared(name, info, ctor, normalized.args, call.span, &resultType);
    }

    struct Match {
        const ConstructorDeclNode* ctor = nullptr;
        NormalizedPreparedCallArgs args;
    };

    QList<Match> matches;
    int bestScore = 1'000'000;
    for (const ConstructorDeclNode* ctor : info.constructors) {
        if (!ctor)
            continue;
        NormalizedPreparedCallArgs normalized = normalizePreparedCallArgsForParams(*info.decl,
                                                                                   name,
                                                                                   ctor->params,
                                                                                   call,
                                                                                   rawArgs,
                                                                                   false,
                                                                                   false,
                                                                                   rawPipeHoles);
        if (!normalized.ok)
            continue;
        const bool variadic = !ctor->params.empty() && ctor->params.back()->variadic;
        const size_t fixedCount = variadic ? ctor->params.size() - 1 : ctor->params.size();

        int score = variadic ? 4 : 0;
        bool ok = true;
        for (size_t i = 0; i < fixedCount; ++i) {
            const AbelType paramType = typeFromAstForDecl(*ctor->params[i]->type, *info.decl);
            const auto argScore = scorePreparedArgument(paramType, normalized.args[i]);
            if (!argScore) {
                ok = false;
                break;
            }
            score += *argScore + (normalized.defaulted[i] ? 2 : 0);
        }
        if (!ok)
            continue;

        if (variadic) {
            const AbelType variadicType = typeFromAstForDecl(*ctor->params.back()->type, *info.decl);
            if (variadicType.kind != TypeKind::Any)
                continue;
            score += static_cast<int>(normalized.args.size() - fixedCount) * 4;
        }

        if (score < bestScore) {
            bestScore = score;
            matches.clear();
            matches.push_back(Match{ctor, std::move(normalized)});
        } else if (score == bestScore) {
            matches.push_back(Match{ctor, std::move(normalized)});
        }
    }

    if (matches.isEmpty()) {
        if (callHasStructuredArgs(call)) {
            for (const ConstructorDeclNode* ctor : info.constructors) {
                if (!ctor)
                    continue;
                normalizePreparedCallArgsForParams(*info.decl, name, ctor->params, call, rawArgs, true, false, rawPipeHoles);
                if (m_ctx->hasError())
                    return AbelValue::makeUnknown();
            }
        }
        error(QStringLiteral("E0576"),
              QStringLiteral("no matching constructor '%1' overload for %2 argument(s)")
                  .arg(name)
                  .arg(call.args.size()),
              call.span);
        return AbelValue::makeUnknown();
    }
    if (matches.size() > 1) {
        error(QStringLiteral("E0576"), QStringLiteral("constructor '%1' overload is ambiguous").arg(name), call.span);
        return AbelValue::makeUnknown();
    }

    const ConstructorDeclNode* ctor = matches.front().ctor;
    NormalizedPreparedCallArgs normalized = normalizePreparedCallArgsForParams(*info.decl,
                                                                               name,
                                                                               ctor->params,
                                                                               call,
                                                                               rawArgs,
                                                                               true,
                                                                               true,
                                                                               rawPipeHoles);
    if (!normalized.ok || m_ctx->hasError())
        return AbelValue::makeUnknown();
    if (rejectMultipleMutableRefHoles(*ctor, normalized))
        return AbelValue::makeUnknown();
    return evalStructConstructorPrepared(name, info, ctor, normalized.args, call.span, &resultType);
}

AbelValue Interpreter::evalStructConstructorPrepared(const QString& name,
                                                     const StructRuntimeInfo& info,
                                                     const ConstructorDeclNode* ctor,
                                                     const std::vector<PreparedCallArg>& args,
                                                     const SourceSpan& span,
                                                     const AbelType* constructedType)
{
    DeclContextGuard context(m_currentPackage, m_currentModule, m_currentImports, m_currentImportAliases, *info.decl);
    const AbelType resultType = constructedType ? *constructedType : makeStructType(structTypeName(*info.decl));
    if (!ctor)
        return AbelValue::makeUnknown();
    if (ctor->isPrivate && !isCurrentStruct(info)) {
        error(QStringLiteral("E0576"), QStringLiteral("constructor '%1' is private").arg(name), span);
        return AbelValue::makeUnknown();
    }
    if (args.empty() && !ctor->params.empty()) {
        error(QStringLiteral("E0588"), QStringLiteral("constructor '%1' is not default-constructible").arg(name), span);
        return AbelValue::makeUnknown();
    }
    const bool variadic = !ctor->params.empty() && ctor->params.back()->variadic;
    const size_t fixedCount = variadic ? ctor->params.size() - 1 : ctor->params.size();
    if ((!variadic && args.size() != ctor->params.size()) || (variadic && args.size() < fixedCount)) {
        error(QStringLiteral("E0576"), QStringLiteral("constructor '%1' called with wrong argument count").arg(name), span);
        return AbelValue::makeUnknown();
    }

    struct BoundArg {
        AbelValue value;
        AbelLocation* location = nullptr;
        bool byReference = false;
        bool isConst = false;
    };
    std::vector<BoundArg> bound(fixedCount);
    for (size_t i = 0; i < fixedCount; ++i) {
        const ParameterNode& p = *ctor->params[i];
        const AbelType target = typeFromAstInCurrentPackage(*p.type);
        bound[i].isConst = isReadOnlyBinding(target, p.type->isConst);
        if (target.isReference()) {
            if (!args[i].location) {
                error(QStringLiteral("E0576"), QStringLiteral("constructor parameter '%1' requires lvalue").arg(p.name), args[i].span);
                continue;
            }
            if (!bound[i].isConst && args[i].isReadOnly) {
                error(QStringLiteral("E0576"),
                      QStringLiteral("non-const constructor parameter '%1' cannot bind to const lvalue").arg(p.name),
                      args[i].span);
                continue;
            }
            if (!canBindReferenceValue(target, args[i].value.type())) {
                error(QStringLiteral("E0576"),
                      QStringLiteral("cannot bind constructor parameter '%1' of type %2 to %3 lvalue")
                          .arg(p.name, target.displayName(), args[i].value.type().displayName()),
                      args[i].span);
                continue;
            }
            bound[i].location = args[i].location;
            bound[i].byReference = true;
        } else {
            bound[i].value = convertOrError(args[i].value, target, args[i].span);
        }
    }
    std::vector<AbelValue> packed;
    if (variadic) {
        const ParameterNode& p = *ctor->params.back();
        if (typeFromAstInCurrentPackage(*p.type).kind != TypeKind::Any) {
            error(QStringLiteral("E0560"), QStringLiteral("only any... variadic parameters are supported"), p.span);
        } else {
            packed.reserve(args.size() - fixedCount);
            for (size_t i = fixedCount; i < args.size(); ++i)
                packed.push_back(AbelValue::makeAny(args[i].value));
        }
    }
    if (m_ctx->hasError())
        return AbelValue::makeUnknown();

    QHash<QString, AbelValue> fields;
    std::vector<QString> order;
    order.reserve(info.decl->fields.size());
    for (const auto& field : info.decl->fields) {
        const AbelType type = typeFromAstInCurrentPackage(*field->type);
        order.push_back(field->name);
        fields.insert(field->name, defaultValueForType(type));
    }
    AbelValue object = AbelValue::makeStruct(resultType.spelling, order, std::move(fields));
    AbelLocation* self = m_ctx->createStorage(object);

    CurrentStructGuard structGuard(m_currentStruct, structTypeName(*info.decl));
    RuntimeFrameGuard frame(*m_ctx, true, constructorFrameSymbol(name), span);
    m_ctx->defineVariable(QStringLiteral("this"), self, false, false, span);
    auto structValue = self->read().asStruct();
    for (const auto& fieldName : structValue->fieldOrder)
        m_ctx->defineVariable(fieldName,
                              m_ctx->createStructFieldLocation(structValue.get(),
                                                               fieldName,
                                                               structFieldReadOnly(object.type(), fieldName),
                                                               structFieldType(object.type(), fieldName)),
                              structFieldReadOnly(object.type(), fieldName),
                              false,
                              span);
    m_ctx->pushFrame();
    for (size_t i = 0; i < fixedCount; ++i) {
        const ParameterNode& p = *ctor->params[i];
        if (bound[i].byReference)
            m_ctx->defineVariable(p.name, bound[i].location, bound[i].isConst, true, p.span);
        else
            m_ctx->defineValueVariable(p.name, bound[i].value, bound[i].isConst, p.span);
    }
    if (variadic) {
        const ParameterNode& p = *ctor->params.back();
        m_ctx->defineValueVariable(p.name, AbelValue::makeVector(makeType(TypeKind::Any), std::move(packed)), false, p.span);
    }
    if (m_ctx->hasError()) {
        m_ctx->popFrame();
        return AbelValue::makeUnknown();
    }

    ExecResult flow = execBlock(*ctor->body);
    m_ctx->popFrame();
    if (m_ctx->hasError())
        return AbelValue::makeUnknown();
    if (flow.kind == FlowKind::Return)
        error(QStringLiteral("E0577"), QStringLiteral("constructor cannot return a value"), span);
    return m_ctx->hasError() ? AbelValue::makeUnknown() : self->read();
}

AbelValue Interpreter::evalStructMethod(const FieldAccessExprNode& callee,
                                        const CallExprNode& call,
                                        bool receiverPipeHole,
                                        const std::vector<bool>* rawPipeHoles,
                                        int totalPipeHoles)
{
    AbelLocation* self = evalLocation(*callee.base);
    if (!self)
        return AbelValue::makeUnknown();
    AbelValue receiver = self->read();
    if (receiver.type().kind != TypeKind::Struct) {
        error(QStringLiteral("E0578"), QStringLiteral("method receiver is not struct"), callee.span);
        return AbelValue::makeUnknown();
    }
    const StructRuntimeInfo* info = structInfoForType(receiver.type());
    if (!info) {
        error(QStringLiteral("E0579"), QStringLiteral("unknown struct type '%1'").arg(receiver.type().displayName()), callee.span);
        return AbelValue::makeUnknown();
    }
    const QList<const FunctionDeclNode*> methods = info->methods.value(callee.field);
    if (methods.isEmpty()) {
        error(QStringLiteral("E0579"), QStringLiteral("unknown method '%1'").arg(callee.field), callee.span);
        return AbelValue::makeUnknown();
    }
    QList<const FunctionDeclNode*> visible;
    for (const FunctionDeclNode* method : methods) {
        if (!method)
            continue;
        if (method->isPrivate && !isCurrentStruct(*info))
            continue;
        visible.push_back(method);
    }
    if (visible.isEmpty()) {
        error(QStringLiteral("E0579"), QStringLiteral("method '%1' is private").arg(callee.field), callee.span);
        return AbelValue::makeUnknown();
    }
    return callStructFunctionOverloadExpr(callee.field,
                                          visible,
                                          self,
                                          call,
                                          call.span,
                                          receiverPipeHole,
                                          rawPipeHoles,
                                          totalPipeHoles).value;
}

AbelValue Interpreter::evalBuiltinMethod(const FieldAccessExprNode& callee,
                                         const std::vector<std::unique_ptr<ExprNode>>& args,
                                         bool receiverPipeHole,
                                         int totalPipeHoles)
{
    AbelValue receiver;
    AbelLocation* baseLoc = nullptr;
    if (exprCanHaveRuntimeLocation(*callee.base)) {
        baseLoc = evalLocation(*callee.base);
        if (!baseLoc)
            return AbelValue::makeUnknown();
        receiver = baseLoc->read();
    } else {
        receiver = evalExpr(*callee.base);
    }
    if (receiver.type().kind == TypeKind::Unknown)
        return AbelValue::makeUnknown();
    if (receiverPipeHole && totalPipeHoles > 1) {
        if (const BuiltinMethodDesc* method = m_builtins.methodDescriptor(receiver.type(), callee.field)) {
            if (method->mutatesReceiver) {
                error(QStringLiteral("E0401"),
                      QStringLiteral("pipe RHS cannot use the same hole multiple times when any use is mutable"),
                      callee.span);
                return AbelValue::makeUnknown();
            }
        }
    }
    if (!baseLoc)
        baseLoc = m_ctx->createStorage(receiver, receiver.type().isConst);
    BuiltinMethodCall call{
        *m_ctx,
        baseLoc,
        baseLoc->read(),
        callee.field,
        {},
        {},
        callee.span,
        [this](const AbelType& type, const SourceSpan& span) {
            return defaultConstructValue(type, span);
        },
    };
    call.args.reserve(args.size());
    call.argSpans.reserve(args.size());
    for (const auto& arg : args) {
        call.args.push_back(evalExpr(*arg));
        call.argSpans.push_back(arg->span);
    }
    return m_builtins.callMethod(std::move(call));
}

AbelValue Interpreter::evalAssignment(const AssignExprNode& expr)
{
    AbelLocation* lhs = evalLocation(*expr.lhs);
    if (!lhs) {
        error(QStringLiteral("E0526"), QStringLiteral("left side of assignment is not an lvalue"), expr.span);
        return AbelValue::makeUnknown();
    }
    const AbelType targetType = lhs->declaredType.kind != TypeKind::Unknown ? lhs->declaredType : lhs->read().type();
    if (lhs->isReadOnly) {
        error(QStringLiteral("E0526"), QStringLiteral("cannot assign to readonly lvalue"), expr.span);
        return AbelValue::makeUnknown();
    }
    if (expr.op == QStringLiteral("=")) {
        AbelValue rhs = convertOrError(evalExpr(*expr.rhs), targetType, expr.rhs->span);
        lhs->write(rhs);
        return rhs;
    }
    if (!isCompoundAssignmentOperator(expr.op)) {
        error(QStringLiteral("E0526"), QStringLiteral("unknown assignment operator '%1'").arg(expr.op), expr.span);
        return AbelValue::makeUnknown();
    }

    const AbelValue current = lhs->read();
    const AbelValue rhsRaw = evalExpr(*expr.rhs);
    if (current.type().kind == TypeKind::Unknown || rhsRaw.type().kind == TypeKind::Unknown)
        return AbelValue::makeUnknown();

    if (auto overloaded = evalUserCompoundAssignmentOperator(expr.op, *lhs, current, rhsRaw, expr.span))
        return *overloaded;

    AbelValue combined = evalCompoundAssignmentFallback(expr.op, current, rhsRaw, expr.span);
    AbelValue converted = convertOrError(combined, targetType, expr.span);
    lhs->write(converted);
    return converted;
}

AbelValue Interpreter::defaultConstructValue(const AbelType& type, const SourceSpan& span)
{
    QSet<QString> visiting;
    return defaultConstructValue(type, span, visiting);
}

AbelValue Interpreter::defaultConstructValue(const AbelType& type, const SourceSpan& span, QSet<QString>& visiting)
{
    if (type.kind == TypeKind::Struct) {
        const StructRuntimeInfo* info = structInfoForType(type);
        if (!info || !info->decl) {
            error(QStringLiteral("E0589"), QStringLiteral("unknown struct '%1'").arg(type.displayName()), span);
            return AbelValue::makeUnknown();
        }
        if (visiting.contains(type.spelling)) {
            error(QStringLiteral("E0590"), QStringLiteral("recursive default construction of '%1' is not supported").arg(type.displayName()), span);
            return AbelValue::makeUnknown();
        }
        const ConstructorDeclNode* defaultCtor = nullptr;
        if (!info->constructors.isEmpty()) {
            for (const ConstructorDeclNode* ctor : info->constructors) {
                if (ctor && ctor->params.empty()) {
                    defaultCtor = ctor;
                    break;
                }
            }
            if (!defaultCtor) {
                error(QStringLiteral("E0588"), QStringLiteral("constructor '%1' is not default-constructible").arg(type.displayName()), span);
                return AbelValue::makeUnknown();
            }
            if (defaultCtor->isPrivate && !isCurrentStruct(*info)) {
                error(QStringLiteral("E0588"), QStringLiteral("default constructor for %1 is private").arg(type.displayName()), span);
                return AbelValue::makeUnknown();
            }
        }

        visiting.insert(type.spelling);
        QHash<QString, AbelValue> fields;
        std::vector<QString> order;
        order.reserve(info->decl->fields.size());
        for (const auto& field : info->decl->fields) {
            const AbelType fieldType = typeFromAstForDecl(*field->type, *info->decl);
            order.push_back(field->name);
            fields.insert(field->name,
                          defaultCtor
                              ? defaultValueForType(fieldType)
                              : defaultConstructValue(fieldType, field->span, visiting));
            if (m_ctx->hasError()) {
                visiting.remove(type.spelling);
                return AbelValue::makeUnknown();
            }
        }
        AbelValue object = AbelValue::makeStruct(type.spelling, order, std::move(fields));
        if (defaultCtor) {
            AbelLocation* self = m_ctx->createStorage(object);
            CurrentStructGuard structGuard(m_currentStruct, structTypeName(*info->decl));
            RuntimeFrameGuard frame(*m_ctx, true, constructorFrameSymbol(type.spelling), span);
            m_ctx->defineVariable(QStringLiteral("this"), self, false, false, span);
            auto structValue = self->read().asStruct();
            for (const auto& fieldName : structValue->fieldOrder)
                m_ctx->defineVariable(fieldName,
                                      m_ctx->createStructFieldLocation(structValue.get(),
                                                                       fieldName,
                                                                       structFieldReadOnly(object.type(), fieldName),
                                                                       structFieldType(object.type(), fieldName)),
                                      structFieldReadOnly(object.type(), fieldName),
                                      false,
                                      span);
            if (!m_ctx->hasError()) {
                DeclContextGuard context(m_currentPackage, m_currentModule, m_currentImports, m_currentImportAliases, *info->decl);
                m_ctx->pushFrame();
                ExecResult flow = execBlock(*defaultCtor->body);
                m_ctx->popFrame();
                if (flow.kind == FlowKind::Return)
                    error(QStringLiteral("E0577"), QStringLiteral("constructor cannot return a value"), span);
            }
            object = m_ctx->hasError() ? AbelValue::makeUnknown() : self->read();
        }
        visiting.remove(type.spelling);
        return object;
    }
    return defaultValueForType(type);
}

std::optional<QString> Interpreter::stringifyValue(const AbelValue& value, const SourceSpan& span)
{
    if (value.type().kind != TypeKind::Struct)
        return std::nullopt;

    for (const FunctionDeclNode* fn : m_functions.value(QStringLiteral("to_str"))) {
        if (fn->params.size() != 1 || fn->params.front()->variadic)
            continue;
        if (!isDeclVisible(*fn, fn->exported))
            continue;

        const AbelType returnType = typeFromAstForDecl(*fn->returnType, *fn);
        if (returnType.kind != TypeKind::Str)
            continue;

        const AbelType paramType = typeFromAstForDecl(*fn->params.front()->type, *fn);
        if (!canAssignValue(paramType, value.type()))
            continue;

        ExecResult result = callFunction(*fn, {value});
        if (m_ctx->hasError())
            return std::nullopt;
        AbelValue text = convertOrError(result.value, makeType(TypeKind::Str), span);
        if (m_ctx->hasError())
            return std::nullopt;
        return text.asString();
    }
    return std::nullopt;
}

bool Interpreter::isDeclInCurrentModule(const DeclNode& decl, const QString& packageName) const
{
    const QString package = packageName.isNull() ? m_currentPackage : packageName;
    if (decl.packageName != package)
        return false;
    if (m_currentModule.isEmpty() || decl.moduleName.isEmpty())
        return true;
    return decl.moduleName == m_currentModule;
}

bool Interpreter::isModuleImported(const QString& moduleName) const
{
    return !moduleName.isEmpty() && m_currentImports.contains(moduleName);
}

QString Interpreter::resolveModuleName(const QString& moduleName) const
{
    return m_currentImportAliases.value(moduleName, moduleName);
}

bool Interpreter::isDeclVisible(const DeclNode& decl, bool exportedSymbol) const
{
    if (isDeclInCurrentModule(decl))
        return true;
    if (m_currentModule.isEmpty() || decl.moduleName.isEmpty()) {
        if (decl.packageName == m_currentPackage)
            return true;
        return exportedSymbol && isModuleImported(decl.moduleName);
    }
    if (!isModuleImported(decl.moduleName))
        return false;
    if (decl.packageName == m_currentPackage)
        return true;
    return exportedSymbol;
}

bool Interpreter::isEnumVisible(const EnumDeclNode& decl) const
{
    return isDeclVisible(decl, decl.exported);
}

bool Interpreter::isTypeAliasVisible(const TypeAliasDeclNode& alias) const
{
    return isDeclVisible(alias, alias.exported);
}

AbelType Interpreter::structFieldType(const AbelType& structType, const QString& fieldName)
{
    const StructRuntimeInfo* info = structInfoForType(structType);
    if (!info || !info->decl)
        return makeType(TypeKind::Unknown);
    for (const auto& field : info->decl->fields) {
        if (field->name == fieldName)
            return typeFromAstForDecl(*field->type, *info->decl);
    }
    return makeType(TypeKind::Unknown);
}

bool Interpreter::structFieldReadOnly(const AbelType& structType, const QString& fieldName)
{
    const StructRuntimeInfo* info = structInfoForType(structType);
    if (!info || !info->decl)
        return false;
    for (const auto& field : info->decl->fields) {
        if (field->name != fieldName)
            continue;
        const AbelType fieldType = typeFromAstForDecl(*field->type, *info->decl);
        return isReadOnlyBinding(fieldType, field->type->isConst);
    }
    return false;
}

bool Interpreter::structFieldPrivate(const AbelType& structType, const QString& fieldName) const
{
    const StructRuntimeInfo* info = structInfoForType(structType);
    if (!info || !info->decl)
        return false;
    for (const auto& field : info->decl->fields) {
        if (field->name == fieldName)
            return field->isPrivate;
    }
    return false;
}

bool Interpreter::isCurrentStruct(const StructRuntimeInfo& info) const
{
    return info.decl && m_currentStruct == structTypeName(*info.decl);
}

void Interpreter::attachStringifier(BuiltinFunctionCall& call)
{
    call.stringify = [this](const AbelValue& value, const SourceSpan& span) {
        return stringifyValue(value, span);
    };
    call.readToken = [this](const SourceSpan& span) {
        return readScanToken(span);
    };
}

std::optional<QString> Interpreter::readScanToken(const SourceSpan& span)
{
    static QTextStream in(stdin);
    QString token;
    in >> token;
    if (token.isNull()) {
        error(QStringLiteral("E0429"), QStringLiteral("scan reached end of input"), span);
        return std::nullopt;
    }
    return token;
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
    if (!target.isReference() && value.type().kind == TypeKind::Any) {
        auto converted = dynamicCastValue(value, target, span);
        return converted.value_or(AbelValue::makeUnknown());
    }
    if (!canAssignValue(target, value.type())) {
        error(QStringLiteral("E0531"),
              QStringLiteral("cannot assign %1 to %2").arg(value.type().displayName(), target.displayName()),
              span);
        return AbelValue::makeUnknown();
    }
    return convertValue(value, target);
}

bool Interpreter::isReadOnlyBinding(const AbelType& type, bool syntacticConst) const
{
    return syntacticConst || type.isConst || (type.isReference() && type.pointee && type.pointee->isConst);
}

bool Interpreter::canBindReferenceValue(const AbelType& referenceType, const AbelType& sourceType) const
{
    if (!referenceType.isReference() || !referenceType.pointee)
        return false;
    AbelType referred = *referenceType.pointee;
    referred.isConst = false;
    return canAssignValue(referred, sourceType);
}

std::optional<int> Interpreter::scoreValueArgument(const AbelType& paramType, const AbelValue& arg) const
{
    if (arg.type().kind == TypeKind::Unknown)
        return 0;
    if (paramType.isReference()) {
        const bool isConstRef = paramType.pointee && paramType.pointee->isConst;
        if (!isConstRef || !canBindReferenceValue(paramType, arg.type()))
            return std::nullopt;
        AbelType referred = *paramType.pointee;
        referred.isConst = false;
        return referred == arg.type() ? 1 : 2;
    }
    if (arg.type().kind == TypeKind::Any)
        return paramType.kind == TypeKind::Any ? 0 : 3;
    if (!canAssignValue(paramType, arg.type()))
        return std::nullopt;
    return paramType == arg.type() ? 0 : 1;
}

void Interpreter::error(const QString& code, const QString& message, const SourceSpan& span)
{
    m_ctx->error(code, message, span);
}

} // namespace abel
