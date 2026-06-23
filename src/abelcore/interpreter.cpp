#include "abelcore/interpreter.h"

#include <QSet>
#include <QTextStream>

#include <algorithm>
#include <cmath>

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

InterpreterResult Interpreter::run(const ProgramNode& program, BackendRegistry* backendRegistry)
{
    AbelRuntimeContext ctx;
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

    InterpreterResult result;
    if (!collectEnums(program, ctx)) {
        result.exitCode = 1;
        result.diagnostics = ctx.takeDiagnostics();
        m_ctx = nullptr;
        m_activeBackendRegistry = nullptr;
        return result;
    }
    if (!collectTypeAliases(program, ctx)) {
        result.exitCode = 1;
        result.diagnostics = ctx.takeDiagnostics();
        m_ctx = nullptr;
        m_activeBackendRegistry = nullptr;
        return result;
    }
    if (!collectStructs(program, ctx)) {
        result.exitCode = 1;
        result.diagnostics = ctx.takeDiagnostics();
        m_ctx = nullptr;
        m_activeBackendRegistry = nullptr;
        return result;
    }
    if (!collectFunctions(program, ctx)) {
        result.exitCode = 1;
        result.diagnostics = ctx.takeDiagnostics();
        m_ctx = nullptr;
        m_activeBackendRegistry = nullptr;
        return result;
    }
    if (!collectBackends(program, ctx)) {
        result.exitCode = 1;
        result.diagnostics = ctx.takeDiagnostics();
        m_ctx = nullptr;
        m_activeBackendRegistry = nullptr;
        return result;
    }

    const FunctionDeclNode* main = findRootFunction(QStringLiteral("main"));
    if (!main) {
        ctx.error(QStringLiteral("E0504"), QStringLiteral("missing fn int main() or fn void main()"), program.span);
        result.exitCode = 1;
        result.diagnostics = ctx.takeDiagnostics();
        m_ctx = nullptr;
        m_activeBackendRegistry = nullptr;
        return result;
    }

    const AbelType mainType = typeFromAstForDecl(*main->returnType, *main);
    if (mainType.kind != TypeKind::I32 && mainType.kind != TypeKind::Void) {
        ctx.error(QStringLiteral("E0505"), QStringLiteral("main must return int or void"), main->span);
        result.exitCode = 1;
        result.diagnostics = ctx.takeDiagnostics();
        m_ctx = nullptr;
        m_activeBackendRegistry = nullptr;
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
    m_activeBackendRegistry = nullptr;
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
        if (!s->constructors.empty())
            info.constructor = s->constructors.front().get();
        for (const auto& method : s->methods)
            info.methods.insert(method->name, method.get());
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
        if (!fn->fromDependency)
            return fn;
    }
    return nullptr;
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
        if (node.isConst)
            base = makeConstType(base);
        for (int i = 0; i < node.pointerDepth; ++i)
            base = makePointerType(base);
        if (node.isReference)
            base = makeReferenceType(base);
        return base;
    }
    if (node.name == QStringLiteral("func") && node.elementType) {
        std::vector<AbelType> params;
        params.reserve(node.functionParamTypes.size());
        for (const auto& param : node.functionParamTypes)
            params.push_back(typeFromAstInPackage(*param, packageName));
        AbelType base = makeFunctionType(typeFromAstInPackage(*node.elementType, packageName), std::move(params));
        if (node.isConst)
            base = makeConstType(base);
        for (int i = 0; i < node.pointerDepth; ++i)
            base = makePointerType(base);
        if (node.isReference)
            base = makeReferenceType(base);
        return base;
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
        if (node.isConst)
            base = makeConstType(base);
        for (int i = 0; i < node.pointerDepth; ++i)
            base = makePointerType(base);
        if (node.isReference)
            base = makeReferenceType(base);
        return base;
    }

    const EnumRuntimeInfo* enumInfo = nullptr;
    if (const auto qualified = splitQualifiedSymbol(node.name))
        enumInfo = resolveEnumInModule(qualified->first, qualified->second);
    else
        enumInfo = resolveEnumInPackage(node.name, packageName);
    if (enumInfo) {
        AbelType base = makeType(TypeKind::I32, enumInfo->decl ? enumInfo->decl->name : node.name);
        if (node.isConst)
            base = makeConstType(base);
        for (int i = 0; i < node.pointerDepth; ++i)
            base = makePointerType(base);
        if (node.isReference)
            base = makeReferenceType(base);
        return base;
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
    if (node.isConst)
        base = makeConstType(base);
    for (int i = 0; i < node.pointerDepth; ++i)
        base = makePointerType(base);
    if (node.isReference)
        base = makeReferenceType(base);
    return base;
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
                              QStringLiteral("backend declaration '%1::%2' does not match bound backend signature")
                                  .arg(backend->name, fn->name),
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

ExecResult Interpreter::callStructFunction(const FunctionDeclNode& fn,
                                           AbelLocation* self,
                                           const std::vector<std::unique_ptr<ExprNode>>& args,
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
                error(QStringLiteral("E0566"),
                      QStringLiteral("non-const method parameter '%1' cannot bind to const lvalue").arg(p.name),
                      args[i]->span);
                continue;
            }
            AbelValue current = loc->read();
            if (!canBindReferenceValue(target, current.type())) {
                error(QStringLiteral("E0566"),
                      QStringLiteral("cannot bind method parameter '%1' of type %2 to %3 lvalue")
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
        packed.reserve(args.size() - fixedCount);
        for (size_t i = fixedCount; i < args.size(); ++i)
            packed.push_back(AbelValue::makeAny(evalExpr(*args[i])));
    }
    if (m_ctx->hasError()) {
        return ExecResult::returned(AbelValue::makeUnknown());
    }

    CurrentStructGuard structGuard(m_currentStruct, selfValue.type().spelling);
    RuntimeFrameGuard frame(*m_ctx, true, methodFrameSymbol(fn), span);
    m_ctx->defineVariable(QStringLiteral("this"), self, fn.isConstMethod || self->isReadOnly, false, span);
    auto object = selfValue.asStruct();
    for (const auto& fieldName : object->fieldOrder)
        m_ctx->defineVariable(fieldName,
                              m_ctx->createStructFieldLocation(object.get(),
                                                               fieldName,
                                                               fn.isConstMethod || self->isReadOnly || structFieldReadOnly(selfValue.type(), fieldName)),
                              fn.isConstMethod || self->isReadOnly || structFieldReadOnly(selfValue.type(), fieldName),
                              false,
                              span);
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
        error(QStringLiteral("E0569"), QStringLiteral("break/continue cannot leave method '%1'").arg(fn.name), fn.span);
        return ExecResult::returned(AbelValue::makeUnknown());
    }
    if (returnType.kind == TypeKind::Void)
        return ExecResult::returned(AbelValue::makeVoid());
    error(QStringLiteral("E0569"), QStringLiteral("method '%1' ended without return").arg(fn.name), fn.span);
    return ExecResult::returned(AbelValue::makeUnknown());
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

AbelValue Interpreter::callFunctionValue(const AbelValue& fnValue,
                                         const std::vector<std::unique_ptr<ExprNode>>& args,
                                         const SourceSpan& span)
{
    if (fnValue.type().kind != TypeKind::Function || !fnValue.type().pointee) {
        error(QStringLiteral("E0580"), QStringLiteral("callee is not a function value"), span);
        return AbelValue::makeUnknown();
    }
    auto function = fnValue.asFunction();
    if (!function || !function->lambda || !function->lambda->ownedBody) {
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
    if (!function || !function->lambda || !function->lambda->ownedBody) {
        error(QStringLiteral("E0581"), QStringLiteral("invalid function value"), span);
        return AbelValue::makeUnknown();
    }

    const size_t argc = restArgs.size() + 1;
    if (argc != fnValue.type().params.size()) {
        error(QStringLiteral("E0582"), QStringLiteral("function value called with wrong argument count"), span);
        return AbelValue::makeUnknown();
    }

    auto argAt = [&](size_t index) -> const ExprNode& {
        return index == 0 ? firstArg : *restArgs[index - 1];
    };

    std::vector<AbelValue> values(argc);
    std::vector<AbelLocation*> locations(argc, nullptr);
    std::vector<bool> byReference(argc, false);
    std::vector<bool> paramConst(argc, false);
    for (size_t i = 0; i < argc; ++i) {
        const AbelType& target = fnValue.type().params[i];
        const ExprNode& arg = argAt(i);
        paramConst[i] = isReadOnlyBinding(target, target.isConst);
        if (target.isReference()) {
            byReference[i] = true;
            AbelLocation* loc = evalLocation(arg);
            if (!loc)
                continue;
            if (!paramConst[i] && loc->isReadOnly) {
                error(QStringLiteral("E0583"), QStringLiteral("non-const function parameter cannot bind to const lvalue"), arg.span);
                continue;
            }
            AbelValue current = loc->read();
            if (!canBindReferenceValue(target, current.type())) {
                error(QStringLiteral("E0583"),
                      QStringLiteral("cannot bind function parameter %1 to %2 lvalue")
                          .arg(target.displayName(), current.type().displayName()),
                      arg.span);
                continue;
            }
            locations[i] = loc;
        } else {
            values[i] = convertOrError(evalExpr(arg), target, arg.span);
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
        if (isSourceLocationBuiltinName(e->name))
            return evalSourceLocationBuiltin(e->name, e->span);

        const VariableSlot* slot = m_ctx->lookupVariable(e->name);
        if (!slot) {
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
        AbelValue self = slot->location->read();
        return AbelValue::makePointer(self.type(), slot->location);
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
        if (structFieldPrivate(base.type(), e->field) && m_currentStruct != base.type().spelling) {
            error(QStringLiteral("E0574"), QStringLiteral("field '%1' is private").arg(e->field), e->span);
            return nullptr;
        }
        const bool fieldReadOnly = baseReadOnly || base.type().isConst || structFieldReadOnly(base.type(), e->field);
        return m_ctx->createStructFieldLocation(object, e->field, fieldReadOnly);
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
        if (base.type().kind != TypeKind::Vector || !base.type().pointee) {
            error(QStringLiteral("E0546"), QStringLiteral("indexing requires vector value"), e->span);
            return nullptr;
        }
        m_ctx->createStorage(base);
        qint64 idx = 0;
        if (!requireInteger(evalExpr(*e->index), e->index->span, idx))
            return nullptr;
        auto vector = base.asVector();
        if (idx < 0 || static_cast<size_t>(idx) >= vector->elements.size()) {
            error(QStringLiteral("E0547"), QStringLiteral("vector index out of range"), e->span);
            return nullptr;
        }
        return m_ctx->createVectorElementLocation(vector.get(), static_cast<size_t>(idx), vectorElementReadOnly(base, baseReadOnly));
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
    if (op == QStringLiteral("+") && lhs.type().kind == TypeKind::Str && rhs.type().kind == TypeKind::Str)
        return AbelValue::makeString(lhs.asString() + rhs.asString());

    if ((op == QStringLiteral("==") || op == QStringLiteral("!=")) && lhs.type().kind == rhs.type().kind) {
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
    }

    if (!lhs.type().isNumeric() || !rhs.type().isNumeric()) {
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

AbelValue Interpreter::evalCast(const CastExprNode& expr)
{
    AbelValue source = evalExpr(*expr.expr);
    const AbelType target = typeFromAstInCurrentPackage(*expr.targetType);

    if (source.type().kind == TypeKind::Any) {
        const AbelValue inner = source.asAny()->value;
        if (!canAssignValue(target, inner.type())) {
            error(QStringLiteral("E0591"),
                  QStringLiteral("cannot cast any containing %1 to %2").arg(inner.type().displayName(), target.displayName()),
                  expr.span);
            return AbelValue::makeUnknown();
        }
        return convertValue(inner, target);
    }

    if (!canAssignValue(target, source.type())) {
        error(QStringLiteral("E0590"),
              QStringLiteral("cannot cast %1 to %2").arg(source.type().displayName(), target.displayName()),
              expr.expr->span);
        return AbelValue::makeUnknown();
    }
    return convertValue(source, target);
}

AbelValue Interpreter::evalPipe(const BinaryExprNode& expr)
{
    QString targetName;
    const std::vector<std::unique_ptr<ExprNode>>* restArgs = nullptr;

    if (auto* name = dynamic_cast<NameExprNode*>(expr.rhs.get())) {
        targetName = name->name;
    } else if (auto* call = dynamic_cast<CallExprNode*>(expr.rhs.get())) {
        auto* name = dynamic_cast<NameExprNode*>(call->callee.get());
        if (!name) {
            error(QStringLiteral("E0592"), QStringLiteral("pipe target call must use a named function"), call->callee->span);
            return AbelValue::makeUnknown();
        }
        targetName = name->name;
        restArgs = &call->args;
    } else {
        error(QStringLiteral("E0593"), QStringLiteral("pipe right side must be f or f(args...)"), expr.rhs->span);
        return AbelValue::makeUnknown();
    }

    static const std::vector<std::unique_ptr<ExprNode>> emptyArgs;
    const auto& args = restArgs ? *restArgs : emptyArgs;

    if (const VariableSlot* slot = m_ctx->lookupVariable(targetName)) {
        AbelValue callee = slot->location ? slot->location->read() : AbelValue::makeUnknown();
        if (callee.type().kind == TypeKind::Function)
            return callFunctionValuePipe(callee, *expr.lhs, args, expr.span);
        error(QStringLiteral("E0594"), QStringLiteral("pipe target variable '%1' is not a function value").arg(targetName), expr.rhs->span);
        return AbelValue::makeUnknown();
    }

    if (const FunctionDeclNode* fn = resolveFunction(targetName))
        return callFunctionPipeExpr(*fn, *expr.lhs, args, expr.span).value;

    if (m_builtins.hasFunction(targetName)) {
        BuiltinFunctionCall call{*m_ctx, targetName, {}, {}, expr.span};
        attachStringifier(call);
        call.args.reserve(args.size() + 1);
        call.argSpans.reserve(args.size() + 1);
        call.args.push_back(evalExpr(*expr.lhs));
        call.argSpans.push_back(expr.lhs->span);
        for (const auto& arg : args) {
            call.args.push_back(evalExpr(*arg));
            call.argSpans.push_back(arg->span);
        }
        if (m_ctx->hasError())
            return AbelValue::makeUnknown();
        return m_builtins.callFunction(std::move(call));
    }

    error(QStringLiteral("E0595"), QStringLiteral("unknown pipe target '%1'").arg(targetName), expr.rhs->span);
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
    if (auto* access = dynamic_cast<StaticAccessExprNode*>(expr.callee.get()))
        return evalStaticCall(*access, expr.args, expr.span);

    if (auto* field = dynamic_cast<FieldAccessExprNode*>(expr.callee.get())) {
        AbelValue receiver = evalExpr(*field->base);
        if (receiver.type().kind == TypeKind::Struct)
            return evalStructMethod(*field, expr.args, expr.span);
        return evalBuiltinMethod(*field, expr.args);
    }

    auto* name = dynamic_cast<NameExprNode*>(expr.callee.get());
    if (!name) {
        AbelValue callee = evalExpr(*expr.callee);
        if (callee.type().kind == TypeKind::Function)
            return callFunctionValue(callee, expr.args, expr.span);
        error(QStringLiteral("E0524"), QStringLiteral("callee is not a function value"), expr.span);
        return AbelValue::makeUnknown();
    }
    if (const VariableSlot* slot = m_ctx->lookupVariable(name->name)) {
        AbelValue callee = slot->location ? slot->location->read() : AbelValue::makeUnknown();
        if (callee.type().kind == TypeKind::Function)
            return callFunctionValue(callee, expr.args, expr.span);
        error(QStringLiteral("E0586"), QStringLiteral("variable '%1' is not a function value").arg(name->name), expr.span);
        return AbelValue::makeUnknown();
    }
    const FunctionDeclNode* fn = resolveFunction(name->name);
    if (!fn) {
        if (const StructRuntimeInfo* info = resolveStruct(name->name))
            return evalStructConstructor(name->name, *info, expr.args, expr.span);
        if (m_builtins.hasFunction(name->name)) {
            BuiltinFunctionCall call{*m_ctx, name->name, {}, {}, expr.span};
            attachStringifier(call);
            call.args.reserve(expr.args.size());
            call.argSpans.reserve(expr.args.size());
            for (const auto& arg : expr.args) {
                call.args.push_back(evalExpr(*arg));
                call.argSpans.push_back(arg->span);
            }
            return m_builtins.callFunction(std::move(call));
        }
        error(QStringLiteral("E0525"), QStringLiteral("unknown function '%1'").arg(name->name), expr.span);
        return AbelValue::makeUnknown();
    }
    return callFunctionExpr(*fn, expr.args, expr.span).value;
}

AbelValue Interpreter::evalStaticCall(const StaticAccessExprNode& callee,
                                      const std::vector<std::unique_ptr<ExprNode>>& args,
                                      const SourceSpan& span)
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
                                                 args,
                                                 span);
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
        return evalQualifiedStructConstructor(moduleName, callee.member, args, span);

    const auto functions = m_functions.value(callee.member);
    for (const FunctionDeclNode* fn : functions) {
        if (fn->moduleName == resolvedModuleName)
            return evalQualifiedFunctionCall(moduleName, callee.member, args, span);
    }

    return evalBackendCallByName(baseName, callee.base->span, callee.member, args, span);
}

AbelValue Interpreter::evalBackendCall(const StaticAccessExprNode& callee,
                                       const std::vector<std::unique_ptr<ExprNode>>& args,
                                       const SourceSpan& span)
{
    auto* backendName = dynamic_cast<NameExprNode*>(callee.base.get());
    if (!backendName) {
        error(QStringLiteral("E0603"), QStringLiteral("backend call receiver must be a backend name"), callee.span);
        return AbelValue::makeUnknown();
    }
    return evalBackendCallByName(backendName->name, backendName->span, callee.member, args, span);
}

AbelValue Interpreter::evalBackendCallByName(const QString& backendName,
                                             const SourceSpan& backendSpan,
                                             const QString& member,
                                             const std::vector<std::unique_ptr<ExprNode>>& args,
                                             const SourceSpan& span)
{
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
    const bool variadic = !fn->params.empty() && fn->params.back()->variadic;
    const size_t fixedCount = variadic ? fn->params.size() - 1 : fn->params.size();
    if ((!variadic && args.size() != fn->params.size()) || (variadic && args.size() < fixedCount)) {
        error(QStringLiteral("E0606"), QStringLiteral("backend function '%1::%2' called with wrong argument count").arg(backendName, member), span);
        return AbelValue::makeUnknown();
    }

    std::vector<AbelValue> values;
    std::vector<AbelLocation*> locations;
    values.reserve(args.size());
    locations.reserve(args.size());
    for (size_t i = 0; i < fixedCount; ++i) {
        const ParameterNode& param = *fn->params[i];
        const AbelType target = typeFromAstInCurrentPackage(*param.type);
        if (target.isReference()) {
            AbelLocation* loc = evalLocation(*args[i]);
            if (!loc) {
                values.push_back(AbelValue::makeUnknown());
                locations.push_back(nullptr);
                continue;
            }
            const bool paramConst = isReadOnlyBinding(target, param.type->isConst);
            if (!paramConst && loc->isReadOnly) {
                error(QStringLiteral("E0609"),
                      QStringLiteral("non-const backend parameter '%1' cannot bind to const lvalue").arg(param.name),
                      args[i]->span);
                values.push_back(AbelValue::makeUnknown());
                locations.push_back(nullptr);
                continue;
            }
            AbelValue current = loc->read();
            if (!canBindReferenceValue(target, current.type())) {
                error(QStringLiteral("E0609"),
                      QStringLiteral("cannot bind backend parameter '%1' of type %2 to %3 lvalue")
                          .arg(param.name, target.displayName(), current.type().displayName()),
                      args[i]->span);
                values.push_back(AbelValue::makeUnknown());
                locations.push_back(nullptr);
                continue;
            }
            values.push_back(convertOrError(current, *target.pointee, args[i]->span));
            locations.push_back(loc);
        } else {
            values.push_back(convertOrError(evalExpr(*args[i]), target, args[i]->span));
            locations.push_back(nullptr);
        }
    }
    if (variadic) {
        for (size_t i = fixedCount; i < args.size(); ++i) {
            values.push_back(AbelValue::makeAny(evalExpr(*args[i])));
            locations.push_back(nullptr);
        }
    }
    if (m_ctx->hasError())
        return AbelValue::makeUnknown();

    QList<Diagnostic> diagnostics;
    RuntimeFrameGuard frame(*m_ctx, true, backendFrameSymbol(backendName, member), span);
    AbelValue result = m_activeBackendRegistry->call({simpleBackendName, member, std::move(values), span, std::move(locations)}, diagnostics, m_ctx);
    for (const auto& diagnostic : diagnostics)
        m_ctx->error(diagnostic.code, diagnostic.message, diagnostic.primary);
    return result;
}

AbelValue Interpreter::evalQualifiedFunctionCall(const QString& moduleName,
                                                 const QString& name,
                                                 const std::vector<std::unique_ptr<ExprNode>>& args,
                                                 const SourceSpan& span)
{
    const FunctionDeclNode* fn = resolveFunctionInModule(moduleName, name);
    if (!fn) {
        error(QStringLiteral("E0525"), QStringLiteral("unknown function '%1::%2'").arg(moduleName, name), span);
        return AbelValue::makeUnknown();
    }
    return callFunctionExpr(*fn, args, span).value;
}

AbelValue Interpreter::evalQualifiedStructConstructor(const QString& moduleName,
                                                      const QString& name,
                                                      const std::vector<std::unique_ptr<ExprNode>>& args,
                                                      const SourceSpan& span)
{
    const StructRuntimeInfo* info = resolveStructInModule(moduleName, name);
    if (!info) {
        error(QStringLiteral("E0580"), QStringLiteral("unknown struct '%1::%2'").arg(moduleName, name), span);
        return AbelValue::makeUnknown();
    }
    return evalStructConstructor(moduleName + QStringLiteral("::") + name, *info, args, span);
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
    return AbelValue::makeFunction(makeFunctionType(returnType, std::move(params)), std::move(function));
}

AbelValue Interpreter::evalStructConstructor(const QString& name,
                                             const StructRuntimeInfo& info,
                                             const std::vector<std::unique_ptr<ExprNode>>& args,
                                             const SourceSpan& span)
{
    DeclContextGuard context(m_currentPackage, m_currentModule, m_currentImports, m_currentImportAliases, *info.decl);
    QHash<QString, AbelValue> fields;
    std::vector<QString> order;
    order.reserve(info.decl->fields.size());

    if (!info.constructor) {
        if (args.empty())
            return defaultConstructValue(makeStructType(structTypeName(*info.decl)), span);
        if (args.size() != info.decl->fields.size()) {
            error(QStringLiteral("E0575"), QStringLiteral("constructor '%1' expects %2 argument(s)").arg(name).arg(info.decl->fields.size()), span);
            return AbelValue::makeUnknown();
        }
        for (size_t i = 0; i < args.size(); ++i) {
            const auto& field = info.decl->fields[i];
            if (field->isPrivate && !isCurrentStruct(info)) {
                error(QStringLiteral("E0575"), QStringLiteral("field '%1' is private").arg(field->name), args[i]->span);
                continue;
            }
            order.push_back(field->name);
            AbelValue value = convertOrError(evalExpr(*args[i]), typeFromAstInCurrentPackage(*field->type), args[i]->span);
            fields.insert(field->name, value);
        }
        if (m_ctx->hasError())
            return AbelValue::makeUnknown();
        return AbelValue::makeStruct(structTypeName(*info.decl), order, std::move(fields));
    }

    if (info.constructor->isPrivate && !isCurrentStruct(info)) {
        error(QStringLiteral("E0576"), QStringLiteral("constructor '%1' is private").arg(name), span);
        return AbelValue::makeUnknown();
    }
    if (args.empty() && !info.constructor->params.empty()) {
        error(QStringLiteral("E0588"), QStringLiteral("constructor '%1' is not default-constructible").arg(name), span);
        return AbelValue::makeUnknown();
    }
    if (args.size() != info.constructor->params.size()) {
        error(QStringLiteral("E0576"), QStringLiteral("constructor '%1' called with wrong argument count").arg(name), span);
        return AbelValue::makeUnknown();
    }

    struct PreparedArg {
        AbelValue value;
        AbelLocation* location = nullptr;
        bool byReference = false;
        bool isConst = false;
    };
    std::vector<PreparedArg> prepared(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
        const ParameterNode& p = *info.constructor->params[i];
        const AbelType target = typeFromAstInCurrentPackage(*p.type);
        prepared[i].isConst = isReadOnlyBinding(target, p.type->isConst);
        if (target.isReference()) {
            AbelLocation* loc = evalLocation(*args[i]);
            if (!loc)
                continue;
            if (!prepared[i].isConst && loc->isReadOnly) {
                error(QStringLiteral("E0576"),
                      QStringLiteral("non-const constructor parameter '%1' cannot bind to const lvalue").arg(p.name),
                      args[i]->span);
                continue;
            }
            AbelValue current = loc->read();
            if (!canBindReferenceValue(target, current.type())) {
                error(QStringLiteral("E0576"),
                      QStringLiteral("cannot bind constructor parameter '%1' of type %2 to %3 lvalue")
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
    if (m_ctx->hasError())
        return AbelValue::makeUnknown();

    for (const auto& field : info.decl->fields) {
        const AbelType type = typeFromAstInCurrentPackage(*field->type);
        order.push_back(field->name);
        fields.insert(field->name, defaultValueForType(type));
    }
    AbelValue object = AbelValue::makeStruct(structTypeName(*info.decl), order, std::move(fields));
    AbelLocation* self = m_ctx->createStorage(object);

    CurrentStructGuard structGuard(m_currentStruct, structTypeName(*info.decl));
    RuntimeFrameGuard frame(*m_ctx, true, constructorFrameSymbol(name), span);
    m_ctx->defineVariable(QStringLiteral("this"), self, false, false, span);
    auto structValue = self->read().asStruct();
    for (const auto& fieldName : structValue->fieldOrder)
        m_ctx->defineVariable(fieldName,
                              m_ctx->createStructFieldLocation(structValue.get(),
                                                               fieldName,
                                                               structFieldReadOnly(object.type(), fieldName)),
                              structFieldReadOnly(object.type(), fieldName),
                              false,
                              span);
    for (size_t i = 0; i < args.size(); ++i) {
        const ParameterNode& p = *info.constructor->params[i];
        if (prepared[i].byReference)
            m_ctx->defineVariable(p.name, prepared[i].location, prepared[i].isConst, true, p.span);
        else
            m_ctx->defineValueVariable(p.name, prepared[i].value, prepared[i].isConst, p.span);
    }
    if (m_ctx->hasError())
        return AbelValue::makeUnknown();

    ExecResult flow = execBlock(*info.constructor->body);
    if (m_ctx->hasError())
        return AbelValue::makeUnknown();
    if (flow.kind == FlowKind::Return)
        error(QStringLiteral("E0577"), QStringLiteral("constructor cannot return a value"), span);
    return m_ctx->hasError() ? AbelValue::makeUnknown() : self->read();
}

AbelValue Interpreter::evalStructMethod(const FieldAccessExprNode& callee,
                                        const std::vector<std::unique_ptr<ExprNode>>& args,
                                        const SourceSpan& span)
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
    const FunctionDeclNode* method = info->methods.value(callee.field, nullptr);
    if (!method) {
        error(QStringLiteral("E0579"), QStringLiteral("unknown method '%1'").arg(callee.field), callee.span);
        return AbelValue::makeUnknown();
    }
    if (method->isPrivate && !isCurrentStruct(*info)) {
        error(QStringLiteral("E0579"), QStringLiteral("method '%1' is private").arg(callee.field), callee.span);
        return AbelValue::makeUnknown();
    }
    if (self->isReadOnly && !method->isConstMethod) {
        error(QStringLiteral("E0579"), QStringLiteral("method '%1' requires mutable receiver").arg(callee.field), callee.span);
        return AbelValue::makeUnknown();
    }
    return callStructFunction(*method, self, args, span).value;
}

AbelValue Interpreter::evalBuiltinMethod(const FieldAccessExprNode& callee, const std::vector<std::unique_ptr<ExprNode>>& args)
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
    auto* name = dynamic_cast<NameExprNode*>(expr.lhs.get());
    if (name) {
        VariableSlot* slot = m_ctx->lookupVariable(name->name);
        if (!slot) {
            error(QStringLiteral("E0527"), QStringLiteral("unknown variable '%1'").arg(name->name), expr.span);
            return AbelValue::makeUnknown();
        }
        AbelValue current = slot->location ? slot->location->read() : AbelValue::makeUnknown();
        AbelValue rhs = convertOrError(evalExpr(*expr.rhs), current.type(), expr.rhs->span);
        m_ctx->assignVariable(name->name, rhs, expr.span);
        return rhs;
    }

    AbelLocation* lhs = evalLocation(*expr.lhs);
    if (!lhs) {
        error(QStringLiteral("E0526"), QStringLiteral("left side of assignment is not an lvalue"), expr.span);
        return AbelValue::makeUnknown();
    }
    AbelValue current = lhs->read();
    AbelValue rhs = convertOrError(evalExpr(*expr.rhs), current.type(), expr.rhs->span);
    if (lhs->isReadOnly) {
        error(QStringLiteral("E0526"), QStringLiteral("cannot assign to readonly lvalue"), expr.span);
        return AbelValue::makeUnknown();
    }
    lhs->write(rhs);
    return rhs;
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
        if (info->constructor && !info->constructor->params.empty()) {
            error(QStringLiteral("E0588"), QStringLiteral("constructor '%1' is not default-constructible").arg(type.displayName()), span);
            return AbelValue::makeUnknown();
        }
        if (info->constructor && info->constructor->isPrivate && !isCurrentStruct(*info)) {
            error(QStringLiteral("E0588"), QStringLiteral("default constructor for %1 is private").arg(type.displayName()), span);
            return AbelValue::makeUnknown();
        }

        visiting.insert(type.spelling);
        QHash<QString, AbelValue> fields;
        std::vector<QString> order;
        order.reserve(info->decl->fields.size());
        for (const auto& field : info->decl->fields) {
            const AbelType fieldType = typeFromAstForDecl(*field->type, *info->decl);
            order.push_back(field->name);
            fields.insert(field->name,
                          info->constructor
                              ? defaultValueForType(fieldType)
                              : defaultConstructValue(fieldType, field->span, visiting));
            if (m_ctx->hasError()) {
                visiting.remove(type.spelling);
                return AbelValue::makeUnknown();
            }
        }
        AbelValue object = AbelValue::makeStruct(type.spelling, order, std::move(fields));
        if (info->constructor) {
            AbelLocation* self = m_ctx->createStorage(object);
            CurrentStructGuard structGuard(m_currentStruct, structTypeName(*info->decl));
            RuntimeFrameGuard frame(*m_ctx, true, constructorFrameSymbol(type.spelling), span);
            m_ctx->defineVariable(QStringLiteral("this"), self, false, false, span);
            auto structValue = self->read().asStruct();
            for (const auto& fieldName : structValue->fieldOrder)
                m_ctx->defineVariable(fieldName,
                                      m_ctx->createStructFieldLocation(structValue.get(),
                                                                       fieldName,
                                                                       structFieldReadOnly(object.type(), fieldName)),
                                      structFieldReadOnly(object.type(), fieldName),
                                      false,
                                      span);
            if (!m_ctx->hasError()) {
                DeclContextGuard context(m_currentPackage, m_currentModule, m_currentImports, m_currentImportAliases, *info->decl);
                ExecResult flow = execBlock(*info->constructor->body);
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

void Interpreter::error(const QString& code, const QString& message, const SourceSpan& span)
{
    m_ctx->error(code, message, span);
}

} // namespace abel
