#include "abelcore/typechecker.h"

#include <QSet>

#include <algorithm>
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

bool isUnknownType(const AbelType& type)
{
    return type.kind == TypeKind::Unknown;
}

bool isTestComparable(const AbelType& lhs, const AbelType& rhs)
{
    if (lhs.kind == TypeKind::Unknown || rhs.kind == TypeKind::Unknown)
        return true;
    if (lhs.kind == TypeKind::Any || rhs.kind == TypeKind::Any)
        return true;
    if (lhs.kind == TypeKind::Reference && lhs.pointee)
        return isTestComparable(*lhs.pointee, rhs);
    if (rhs.kind == TypeKind::Reference && rhs.pointee)
        return isTestComparable(lhs, *rhs.pointee);
    if (lhs.isNumeric() && rhs.isNumeric())
        return true;
    if ((lhs.isPointer() && rhs.kind == TypeKind::Nullptr)
        || (lhs.kind == TypeKind::Nullptr && rhs.isPointer()))
        return true;
    if (lhs.kind != rhs.kind)
        return false;
    if (lhs.kind == TypeKind::Vector)
        return lhs.pointee && rhs.pointee && isTestComparable(*lhs.pointee, *rhs.pointee);
    if (lhs.kind == TypeKind::Struct)
        return lhs.spelling == rhs.spelling;
    return lhs == rhs;
}

bool isBuiltinOrderable(const AbelType& type)
{
    return type.isNumeric()
        || type.kind == TypeKind::Bool
        || type.kind == TypeKind::Char
        || type.kind == TypeKind::Str;
}

bool isScannableType(const AbelType& type)
{
    return type.kind == TypeKind::Bool
        || type.isNumeric()
        || type.kind == TypeKind::Char
        || type.kind == TypeKind::Str
        || type.kind == TypeKind::Any;
}

bool isMathBuiltinName(const QString& name)
{
    return name == QStringLiteral("abs")
        || name == QStringLiteral("sqrt")
        || name == QStringLiteral("floor")
        || name == QStringLiteral("ceil")
        || name == QStringLiteral("round")
        || name == QStringLiteral("trunc")
        || name == QStringLiteral("sin")
        || name == QStringLiteral("cos")
        || name == QStringLiteral("tan")
        || name == QStringLiteral("asin")
        || name == QStringLiteral("acos")
        || name == QStringLiteral("atan")
        || name == QStringLiteral("atan2")
        || name == QStringLiteral("exp")
        || name == QStringLiteral("log")
        || name == QStringLiteral("log10")
        || name == QStringLiteral("pow")
        || name == QStringLiteral("gcd")
        || name == QStringLiteral("lcm")
        || name == QStringLiteral("min")
        || name == QStringLiteral("max")
        || name == QStringLiteral("clamp");
}

bool isFilePathBuiltinName(const QString& name)
{
    return name == QStringLiteral("read_text")
        || name == QStringLiteral("write_text")
        || name == QStringLiteral("append_text")
        || name == QStringLiteral("read_lines")
        || name == QStringLiteral("write_lines")
        || name == QStringLiteral("path_exists")
        || name == QStringLiteral("path_is_file")
        || name == QStringLiteral("path_is_dir")
        || name == QStringLiteral("copy_file")
        || name == QStringLiteral("move_path")
        || name == QStringLiteral("remove_path")
        || name == QStringLiteral("path_join")
        || name == QStringLiteral("path_dirname")
        || name == QStringLiteral("path_basename")
        || name == QStringLiteral("path_ext")
        || name == QStringLiteral("path_absolute")
        || name == QStringLiteral("path_clean")
        || name == QStringLiteral("mkdirs")
        || name == QStringLiteral("current_dir")
        || name == QStringLiteral("env_exists")
        || name == QStringLiteral("env_get");
}

int filePathBuiltinArity(const QString& name)
{
    if (name == QStringLiteral("current_dir"))
        return 0;
    if (name == QStringLiteral("write_text")
        || name == QStringLiteral("append_text")
        || name == QStringLiteral("write_lines")
        || name == QStringLiteral("copy_file")
        || name == QStringLiteral("move_path")
        || name == QStringLiteral("path_join")) {
        return 2;
    }
    return 1;
}

std::optional<AbelType> filePathBuiltinArgType(const QString& name, qsizetype index)
{
    if (name == QStringLiteral("current_dir"))
        return std::nullopt;
    if (index == 0)
        return makeType(TypeKind::Str);
    if (index == 1 && (name == QStringLiteral("write_text")
                       || name == QStringLiteral("append_text")
                       || name == QStringLiteral("copy_file")
                       || name == QStringLiteral("move_path")
                       || name == QStringLiteral("path_join"))) {
        return makeType(TypeKind::Str);
    }
    if (index == 1 && name == QStringLiteral("write_lines"))
        return makeVectorType(makeType(TypeKind::Str));
    return std::nullopt;
}

AbelType filePathBuiltinReturnType(const QString& name)
{
    if (name == QStringLiteral("read_text")
        || name == QStringLiteral("path_join")
        || name == QStringLiteral("path_dirname")
        || name == QStringLiteral("path_basename")
        || name == QStringLiteral("path_ext")
        || name == QStringLiteral("path_absolute")
        || name == QStringLiteral("path_clean")
        || name == QStringLiteral("current_dir")
        || name == QStringLiteral("env_get")) {
        return makeType(TypeKind::Str);
    }
    if (name == QStringLiteral("read_lines"))
        return makeVectorType(makeType(TypeKind::Str));
    if (name == QStringLiteral("path_exists")
        || name == QStringLiteral("path_is_file")
        || name == QStringLiteral("path_is_dir")
        || name == QStringLiteral("env_exists")) {
        return makeType(TypeKind::Bool);
    }
    return makeType(TypeKind::Void);
}

int mathBuiltinArity(const QString& name)
{
    if (name == QStringLiteral("pow")
        || name == QStringLiteral("atan2")
        || name == QStringLiteral("gcd")
        || name == QStringLiteral("lcm")
        || name == QStringLiteral("min")
        || name == QStringLiteral("max")) {
        return 2;
    }
    if (name == QStringLiteral("clamp"))
        return 3;
    return 1;
}

bool mathBuiltinReturnsF64(const QString& name)
{
    return name == QStringLiteral("sqrt")
        || name == QStringLiteral("floor")
        || name == QStringLiteral("ceil")
        || name == QStringLiteral("round")
        || name == QStringLiteral("trunc")
        || name == QStringLiteral("sin")
        || name == QStringLiteral("cos")
        || name == QStringLiteral("tan")
        || name == QStringLiteral("asin")
        || name == QStringLiteral("acos")
        || name == QStringLiteral("atan")
        || name == QStringLiteral("atan2")
        || name == QStringLiteral("exp")
        || name == QStringLiteral("log")
        || name == QStringLiteral("log10")
        || name == QStringLiteral("pow");
}

bool mathBuiltinRequiresInteger(const QString& name)
{
    return name == QStringLiteral("gcd")
        || name == QStringLiteral("lcm");
}

bool isCharBuiltinName(const QString& name)
{
    return name == QStringLiteral("char_code")
        || name == QStringLiteral("char_from_code")
        || name == QStringLiteral("char_is_digit")
        || name == QStringLiteral("char_is_letter")
        || name == QStringLiteral("char_is_alnum")
        || name == QStringLiteral("char_is_space")
        || name == QStringLiteral("char_is_upper")
        || name == QStringLiteral("char_is_lower")
        || name == QStringLiteral("char_upper")
        || name == QStringLiteral("char_lower")
        || name == QStringLiteral("char_to_str");
}

AbelType charBuiltinReturnType(const QString& name)
{
    if (name == QStringLiteral("char_code"))
        return makeType(TypeKind::I32);
    if (name == QStringLiteral("char_is_digit")
        || name == QStringLiteral("char_is_letter")
        || name == QStringLiteral("char_is_alnum")
        || name == QStringLiteral("char_is_space")
        || name == QStringLiteral("char_is_upper")
        || name == QStringLiteral("char_is_lower")) {
        return makeType(TypeKind::Bool);
    }
    if (name == QStringLiteral("char_to_str"))
        return makeType(TypeKind::Str);
    return makeType(TypeKind::Char);
}

bool isAnyBuiltinName(const QString& name)
{
    return name == QStringLiteral("any_type")
        || name == QStringLiteral("any_is")
        || name == QStringLiteral("any_is_bool")
        || name == QStringLiteral("any_is_int")
        || name == QStringLiteral("any_is_double")
        || name == QStringLiteral("any_is_char")
        || name == QStringLiteral("any_is_str")
        || name == QStringLiteral("any_is_vector")
        || name == QStringLiteral("any_is_pointer");
}

int anyBuiltinArity(const QString& name)
{
    return name == QStringLiteral("any_is") ? 2 : 1;
}

AbelType anyBuiltinReturnType(const QString& name)
{
    if (name == QStringLiteral("any_type"))
        return makeType(TypeKind::Str);
    return makeType(TypeKind::Bool);
}

ExprType unknownExprType()
{
    return {makeType(TypeKind::Unknown), ValueCategory::PRValue, false};
}

bool isConstReferenceType(const AbelType& type)
{
    return type.isReference() && type.pointee && type.pointee->isConst;
}

bool isReadOnlyBinding(const AbelType& type, bool syntacticConst)
{
    return syntacticConst || type.isConst || isConstReferenceType(type);
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

AbelType numericBinaryResultType(const AbelType& lhs, const AbelType& rhs)
{
    if (lhs.kind == TypeKind::F64 || rhs.kind == TypeKind::F64)
        return makeType(TypeKind::F64);
    const int width = std::max({32, lhs.integerBitWidth(), rhs.integerBitWidth()});
    const bool unsignedResult = lhs.isUnsignedInteger() || rhs.isUnsignedInteger();
    return makeType(integerTypeForWidth(width, unsignedResult));
}

ExprType callReturnExprType(const AbelType& returnType)
{
    if (returnType.isReference() && returnType.pointee)
        return {*returnType.pointee, ValueCategory::LValue, !returnType.pointee->isConst};
    return {returnType, ValueCategory::PRValue, false};
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

bool exprIsLiteralTrue(const ExprNode& expr)
{
    auto* literal = dynamic_cast<const LiteralExprNode*>(&expr);
    return literal
        && literal->kind == LiteralExprNode::Kind::Bool
        && literal->text == QStringLiteral("true");
}

bool blockAlwaysReturns(const BlockStmtNode& block);

bool stmtAlwaysReturns(const StmtNode& stmt)
{
    if (dynamic_cast<const ReturnStmtNode*>(&stmt))
        return true;

    if (auto* block = dynamic_cast<const BlockStmtNode*>(&stmt))
        return blockAlwaysReturns(*block);

    if (auto* ifStmt = dynamic_cast<const IfStmtNode*>(&stmt)) {
        if (ifStmt->branches.empty())
            return false;
        bool hasElse = false;
        for (const auto& branch : ifStmt->branches) {
            if (!branch.condition)
                hasElse = true;
            if (!branch.body || !blockAlwaysReturns(*branch.body))
                return false;
        }
        return hasElse;
    }

    if (auto* whileStmt = dynamic_cast<const WhileStmtNode*>(&stmt)) {
        return whileStmt->condition
            && exprIsLiteralTrue(*whileStmt->condition)
            && whileStmt->body
            && blockAlwaysReturns(*whileStmt->body);
    }

    return false;
}

bool blockAlwaysReturns(const BlockStmtNode& block)
{
    for (const auto& stmt : block.statements) {
        if (stmt && stmtAlwaysReturns(*stmt))
            return true;
    }
    return false;
}

QString declOrigin(const DeclNode& decl, const QString& symbolName)
{
    const QString qualified = decl.moduleName.isEmpty()
        ? symbolName
        : decl.moduleName + QStringLiteral("::") + symbolName;
    if (decl.packageName.isEmpty())
        return qualified;
    return QStringLiteral("%1 (package %2)").arg(qualified, decl.packageName);
}

template <typename DeclPtr>
QStringList declOriginList(const QList<DeclPtr>& decls, auto nameOf)
{
    QStringList origins;
    for (const auto* decl : decls) {
        if (!decl)
            continue;
        origins.push_back(declOrigin(*decl, nameOf(*decl)));
    }
    origins.removeDuplicates();
    std::sort(origins.begin(), origins.end());
    return origins;
}

template <typename DeclPtr>
QList<SourceSpan> declSpanList(const QList<DeclPtr>& decls)
{
    QList<SourceSpan> spans;
    for (const auto* decl : decls) {
        if (decl)
            spans.push_back(decl->span);
    }
    return spans;
}

QString ambiguityExplanation(const QString& kind, const QStringList& origins)
{
    return QStringLiteral("candidate %1s: %2").arg(kind, origins.join(QStringLiteral(", ")));
}

QStringList qualifySuggestions(const QString& kind, const QStringList& origins)
{
    QStringList suggestions;
    QString example = origins.isEmpty() ? QStringLiteral("<module>::<symbol>") : origins.front();
    const int packageDetail = example.indexOf(QStringLiteral(" (package "));
    if (packageDetail >= 0)
        example = example.left(packageDetail);
    suggestions.push_back(QStringLiteral("Qualify the %1 with one candidate module, for example: %2")
                              .arg(kind, example));
    suggestions.push_back(QStringLiteral("Remove one conflicting use declaration or rename/re-export one candidate."));
    return suggestions;
}

} // namespace

TypeCheckResult TypeChecker::check(const ProgramNode& program)
{
    m_functions.clear();
    m_functionDecls.clear();
    m_enums.clear();
    m_typeAliases.clear();
    m_backends.clear();
    m_scopes.clear();
    m_diagnostics.clear();
    m_currentReturnType = makeType(TypeKind::Void);
    m_currentStruct.clear();
    m_currentPackage.clear();
    m_currentModule.clear();
    m_currentImports.clear();
    m_currentImportAliases.clear();
    m_resolvingTypeAliases.clear();
    m_loopDepth = 0;

    collectEnums(program);
    collectTypeAliases(program);
    collectStructs(program);
    collectFunctions(program);
    collectBackends(program);

    const FunctionDeclNode* main = findRootFunction(QStringLiteral("main"));
    if (!main) {
        error(program.span, QStringLiteral("missing fn int main() or fn void main()"));
    } else {
        const AbelType mainType = typeFromAstForDecl(*main->returnType, *main);
        if (mainType.kind != TypeKind::I32 && mainType.kind != TypeKind::Void)
            error(main->span, QStringLiteral("main must return int or void"));
        if (!main->params.empty())
            error(main->span, QStringLiteral("main must not take parameters in v0"));
    }

    for (const auto* fn : m_functionDecls)
        checkFunction(*fn);
    for (const auto& candidates : m_typeAliases) {
        for (const TypeAliasDeclNode* alias : candidates)
            checkTypeAlias(*alias);
    }
    for (const auto& candidates : m_structs) {
        for (const auto& info : candidates)
            checkStruct(*info.decl);
    }
    for (const auto& candidates : m_backends) {
        for (const auto& info : candidates)
            checkBackend(*info.decl);
    }

    return {m_diagnostics};
}

void TypeChecker::collectEnums(const ProgramNode& program)
{
    m_enums.clear();
    for (const auto& decl : program.declarations) {
        auto* e = dynamic_cast<EnumDeclNode*>(decl.get());
        if (!e)
            continue;
        const auto existing = m_enums.value(e->name);
        bool duplicateInModule = false;
        for (const auto& other : existing) {
            if (other.decl && sameDeclNamespace(*other.decl, *e)) {
                error(e->span, QStringLiteral("duplicate enum '%1' in package '%2' module '%3'")
                                   .arg(e->name, e->packageName, e->moduleName));
                duplicateInModule = true;
                break;
            }
        }
        if (duplicateInModule)
            continue;
        EnumInfo info;
        info.decl = e;
        for (qsizetype i = 0; i < e->enumerators.size(); ++i) {
            const QString name = e->enumerators[i];
            if (info.values.contains(name)) {
                error(e->span, QStringLiteral("duplicate enum enumerator '%1'").arg(name));
                continue;
            }
            info.values.insert(name, static_cast<int>(i));
        }
        m_enums[e->name].push_back(std::move(info));
    }
}

void TypeChecker::collectTypeAliases(const ProgramNode& program)
{
    m_typeAliases.clear();
    for (const auto& decl : program.declarations) {
        auto* alias = dynamic_cast<TypeAliasDeclNode*>(decl.get());
        if (!alias)
            continue;
        const auto existing = m_typeAliases.value(alias->name);
        bool duplicateInModule = false;
        for (const TypeAliasDeclNode* other : existing) {
            if (sameDeclNamespace(*other, *alias)) {
                error(alias->span, QStringLiteral("duplicate type alias '%1' in package '%2' module '%3'")
                                      .arg(alias->name, alias->packageName, alias->moduleName));
                duplicateInModule = true;
                break;
            }
        }
        if (!duplicateInModule)
            m_typeAliases[alias->name].push_back(alias);
    }
}

void TypeChecker::collectStructs(const ProgramNode& program)
{
    m_structs.clear();
    for (const auto& decl : program.declarations) {
        auto* s = dynamic_cast<StructDeclNode*>(decl.get());
        if (!s)
            continue;
        const auto existing = m_structs.value(s->name);
        bool duplicateInModule = false;
        for (const auto& other : existing) {
            if (other.decl && sameDeclNamespace(*other.decl, *s)) {
                error(s->span, QStringLiteral("duplicate struct '%1' in package '%2' module '%3'")
                                    .arg(s->name, s->packageName, s->moduleName));
                duplicateInModule = true;
                break;
            }
        }
        if (duplicateInModule)
            continue;
        StructInfo info;
        info.decl = s;
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
        m_structs[s->name].push_back(std::move(info));
    }

    for (auto& candidates : m_structs) {
        for (auto& info : candidates) {
            const StructDeclNode* s = info.decl;
            if (!s)
                continue;
            for (const auto& field : s->fields) {
                if (info.fields.contains(field->name)) {
                    error(field->span, QStringLiteral("duplicate field '%1'").arg(field->name));
                    continue;
                }
                AbelType type = typeFromAstForDecl(*field->type, *s, false);
                if (type.isReference())
                    error(field->span, QStringLiteral("reference fields are not supported in v0"));
                info.fields.insert(field->name, FieldInfo{type, isReadOnlyBinding(type, field->type->isConst), field->isPrivate});
            }
        }
    }
}

void TypeChecker::collectFunctions(const ProgramNode& program)
{
    for (const auto& decl : program.declarations) {
        if (auto* fn = dynamic_cast<FunctionDeclNode*>(decl.get())) {
            const auto existing = m_functions.value(fn->name);
            bool duplicateInPackage = false;
            for (const FunctionDeclNode* other : existing) {
                if (sameDeclNamespace(*other, *fn)) {
                    if (fn->name == other->name) {
                        if (sameFunctionSignature(*other, *fn)) {
                            if (fn->isOperator) {
                                error(fn->span,
                                      QStringLiteral("duplicate operator '%1' overload with the same signature in package '%2' module '%3'")
                                          .arg(fn->operatorSymbol, fn->packageName, fn->moduleName));
                            } else {
                                error(fn->span,
                                      QStringLiteral("duplicate function '%1' overload with the same signature in package '%2' module '%3'")
                                          .arg(fn->name, fn->packageName, fn->moduleName));
                            }
                            duplicateInPackage = true;
                            break;
                        }
                        continue;
                    }
                    if (fn->isOperator && other->isOperator && fn->operatorSymbol == other->operatorSymbol) {
                        if (sameFunctionSignature(*other, *fn)) {
                            error(fn->span,
                                  QStringLiteral("duplicate operator '%1' overload with the same signature in package '%2' module '%3'")
                                      .arg(fn->operatorSymbol, fn->packageName, fn->moduleName));
                            duplicateInPackage = true;
                            break;
                        }
                        continue;
                    }
                    error(fn->span, QStringLiteral("duplicate function '%1' in package '%2' module '%3'")
                                        .arg(fn->name, fn->packageName, fn->moduleName));
                    duplicateInPackage = true;
                    break;
                }
            }
            if (duplicateInPackage)
                continue;
            m_functions[fn->name].push_back(fn);
            m_functionDecls.push_back(fn);
        }
    }
}

bool TypeChecker::sameFunctionSignature(const FunctionDeclNode& lhs, const FunctionDeclNode& rhs)
{
    if (lhs.name != rhs.name)
        return false;
    if (lhs.params.size() != rhs.params.size())
        return false;
    for (size_t i = 0; i < lhs.params.size(); ++i) {
        if (lhs.params[i]->variadic != rhs.params[i]->variadic)
            return false;
        const AbelType lhsType = typeFromAstForDecl(*lhs.params[i]->type, lhs, false);
        const AbelType rhsType = typeFromAstForDecl(*rhs.params[i]->type, rhs, false);
        if (lhsType != rhsType)
            return false;
    }
    return true;
}

const FunctionDeclNode* TypeChecker::findRootFunction(const QString& name) const
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

QList<const FunctionDeclNode*> TypeChecker::resolveFunctionCandidates(const QString& name, const SourceSpan& span, bool diagnose)
{
    const auto candidates = m_functions.value(name);
    if (candidates.isEmpty())
        return {};

    QList<const FunctionDeclNode*> current;
    QList<const FunctionDeclNode*> visible;
    QList<const FunctionDeclNode*> hidden;
    for (const FunctionDeclNode* fn : candidates) {
        if (isDeclInCurrentModule(*fn))
            current.push_back(fn);
        else if (isFunctionVisible(*fn))
            visible.push_back(fn);
        else
            hidden.push_back(fn);
    }
    if (!current.isEmpty())
        return current;
    if (!visible.isEmpty())
        return visible;
    if (hidden.size() == 1) {
        if (diagnose)
            requireFunctionVisible(*hidden.front(), span);
        return {};
    }
    if (!hidden.isEmpty() && diagnose)
        error(span, QStringLiteral("function '%1' exists but is not visible from current module").arg(name));
    return {};
}

QList<const FunctionDeclNode*> TypeChecker::resolveFunctionCandidatesInModule(const QString& moduleName,
                                                                               const QString& name,
                                                                               const SourceSpan& span,
                                                                               bool diagnose)
{
    const QString resolvedModuleName = resolveModuleName(moduleName);
    const auto candidates = m_functions.value(name);
    if (candidates.isEmpty())
        return {};

    QList<const FunctionDeclNode*> visible;
    QList<const FunctionDeclNode*> hidden;
    for (const FunctionDeclNode* fn : candidates) {
        if (fn->moduleName != resolvedModuleName)
            continue;
        if (isFunctionVisible(*fn))
            visible.push_back(fn);
        else
            hidden.push_back(fn);
    }
    if (!visible.isEmpty())
        return visible;
    if (hidden.size() == 1) {
        if (diagnose)
            requireFunctionVisible(*hidden.front(), span);
        return {};
    }
    if (!hidden.isEmpty() && diagnose)
        error(span, QStringLiteral("function '%1::%2' exists but is not visible").arg(moduleName, name));
    return {};
}

const FunctionDeclNode* TypeChecker::resolveFunction(const QString& name, const SourceSpan& span, bool diagnose)
{
    const auto candidates = m_functions.value(name);
    if (candidates.isEmpty())
        return nullptr;

    QList<const FunctionDeclNode*> current;
    QList<const FunctionDeclNode*> visible;
    QList<const FunctionDeclNode*> hidden;
    for (const FunctionDeclNode* fn : candidates) {
        if (isDeclInCurrentModule(*fn))
            current.push_back(fn);
        else if (isFunctionVisible(*fn))
            visible.push_back(fn);
        else
            hidden.push_back(fn);
    }
    if (current.size() == 1)
        return current.front();
    if (current.size() > 1) {
        if (diagnose) {
            const QStringList origins = declOriginList(current, [](const FunctionDeclNode& fn) { return fn.name; });
            errorDetailed(span,
                          QStringLiteral("function '%1' is ambiguous in current module").arg(name),
                          declSpanList(current),
                          ambiguityExplanation(QStringLiteral("function"), origins),
                          qualifySuggestions(QStringLiteral("function"), origins));
        }
        return nullptr;
    }
    if (visible.size() == 1)
        return visible.front();
    if (visible.size() > 1) {
        if (diagnose) {
            const QStringList origins = declOriginList(visible, [](const FunctionDeclNode& fn) { return fn.name; });
            errorDetailed(span,
                          QStringLiteral("function '%1' is ambiguous across imported modules").arg(name),
                          declSpanList(visible),
                          ambiguityExplanation(QStringLiteral("function"), origins),
                          qualifySuggestions(QStringLiteral("function"), origins));
        }
        return nullptr;
    }
    if (hidden.size() == 1) {
        if (diagnose)
            requireFunctionVisible(*hidden.front(), span);
        return nullptr;
    }
    if (!hidden.isEmpty() && diagnose)
        error(span, QStringLiteral("function '%1' exists but is not visible from current module").arg(name));
    return nullptr;
}

const FunctionDeclNode* TypeChecker::resolveFunctionInModule(const QString& moduleName,
                                                             const QString& name,
                                                             const SourceSpan& span,
                                                             bool diagnose)
{
    const QString resolvedModuleName = resolveModuleName(moduleName);
    const auto candidates = m_functions.value(name);
    if (candidates.isEmpty())
        return nullptr;

    QList<const FunctionDeclNode*> visible;
    QList<const FunctionDeclNode*> hidden;
    for (const FunctionDeclNode* fn : candidates) {
        if (fn->moduleName != resolvedModuleName)
            continue;
        if (isFunctionVisible(*fn))
            visible.push_back(fn);
        else
            hidden.push_back(fn);
    }
    if (visible.size() == 1)
        return visible.front();
    if (visible.size() > 1) {
        if (diagnose) {
            const QStringList origins = declOriginList(visible, [](const FunctionDeclNode& fn) { return fn.name; });
            errorDetailed(span,
                          QStringLiteral("function '%1::%2' is ambiguous").arg(moduleName, name),
                          declSpanList(visible),
                          ambiguityExplanation(QStringLiteral("function"), origins),
                          qualifySuggestions(QStringLiteral("function"), origins));
        }
        return nullptr;
    }
    if (hidden.size() == 1) {
        if (diagnose)
            requireFunctionVisible(*hidden.front(), span);
        return nullptr;
    }
    return nullptr;
}

const TypeChecker::StructInfo* TypeChecker::resolveStruct(const QString& name, const SourceSpan& span, bool diagnose)
{
    if (const auto qualified = splitQualifiedSymbol(name))
        return resolveStructInModule(qualified->first, qualified->second, span, diagnose);
    return resolveStructInPackage(name, m_currentPackage, span, diagnose);
}

const TypeChecker::StructInfo* TypeChecker::resolveStructInModule(const QString& moduleName,
                                                                  const QString& name,
                                                                  const SourceSpan& span,
                                                                  bool diagnose)
{
    const QString resolvedModuleName = resolveModuleName(moduleName);
    auto found = m_structs.constFind(name);
    if (found == m_structs.constEnd() || found->isEmpty())
        return nullptr;

    QList<const StructInfo*> visible;
    QList<const StructInfo*> hidden;
    for (const auto& info : found.value()) {
        if (!info.decl || info.decl->moduleName != resolvedModuleName)
            continue;
        if (isStructVisible(*info.decl))
            visible.push_back(&info);
        else
            hidden.push_back(&info);
    }
    if (visible.size() == 1)
        return visible.front();
    if (visible.size() > 1) {
        if (diagnose) {
            QList<const StructDeclNode*> decls;
            for (const StructInfo* info : visible)
                decls.push_back(info->decl);
            const QStringList origins = declOriginList(decls, [](const StructDeclNode& decl) { return decl.name; });
            errorDetailed(span,
                          QStringLiteral("struct '%1::%2' is ambiguous").arg(moduleName, name),
                          declSpanList(decls),
                          ambiguityExplanation(QStringLiteral("struct"), origins),
                          qualifySuggestions(QStringLiteral("struct"), origins));
        }
        return nullptr;
    }
    if (hidden.size() == 1) {
        if (diagnose)
            requireStructVisible(*hidden.front()->decl, span);
        return nullptr;
    }
    return nullptr;
}

const TypeChecker::StructInfo* TypeChecker::resolveStructInPackage(const QString& name,
                                                                   const QString& packageName,
                                                                   const SourceSpan& span,
                                                                   bool diagnose)
{
    auto found = m_structs.constFind(name);
    if (found == m_structs.constEnd() || found->isEmpty())
        return nullptr;
    const auto& candidates = found.value();

    for (const auto& info : candidates) {
        if (info.decl && isDeclInCurrentModule(*info.decl, packageName))
            return &info;
    }
    QList<const StructInfo*> visible;
    QList<const StructInfo*> hidden;
    for (const auto& info : candidates) {
        if (!info.decl)
            continue;
        if (isStructVisible(*info.decl))
            visible.push_back(&info);
        else
            hidden.push_back(&info);
    }
    if (visible.size() == 1)
        return visible.front();
    if (visible.size() > 1) {
        if (diagnose) {
            QList<const StructDeclNode*> decls;
            for (const StructInfo* info : visible)
                decls.push_back(info->decl);
            const QStringList origins = declOriginList(decls, [](const StructDeclNode& decl) { return decl.name; });
            errorDetailed(span,
                          QStringLiteral("struct '%1' is ambiguous across imported modules").arg(name),
                          declSpanList(decls),
                          ambiguityExplanation(QStringLiteral("struct"), origins),
                          qualifySuggestions(QStringLiteral("struct"), origins));
        }
        return nullptr;
    }
    if (hidden.size() == 1) {
        if (diagnose)
            requireStructVisible(*hidden.front()->decl, span);
        return nullptr;
    }
    if (!hidden.isEmpty() && diagnose)
        error(span, QStringLiteral("struct '%1' exists only as non-exported dependency symbols").arg(name));
    return nullptr;
}

const TypeChecker::StructInfo* TypeChecker::structInfoForType(const AbelType& type) const
{
    if (type.kind != TypeKind::Struct)
        return nullptr;
    for (auto it = m_structs.constBegin(); it != m_structs.constEnd(); ++it) {
        const auto& candidates = it.value();
        for (const auto& info : candidates) {
            if (!info.decl)
                continue;
            if (structTypeName(*info.decl) == type.spelling)
                return &info;
        }
    }
    return nullptr;
}

const TypeChecker::EnumInfo* TypeChecker::resolveEnum(const QString& name, const SourceSpan& span, bool diagnose)
{
    if (const auto qualified = splitQualifiedSymbol(name))
        return resolveEnumInModule(qualified->first, qualified->second, span, diagnose);
    return resolveEnumInPackage(name, m_currentPackage, span, diagnose);
}

const TypeChecker::EnumInfo* TypeChecker::resolveEnumInModule(const QString& moduleName,
                                                              const QString& name,
                                                              const SourceSpan& span,
                                                              bool diagnose)
{
    const QString resolvedModuleName = resolveModuleName(moduleName);
    auto found = m_enums.constFind(name);
    if (found == m_enums.constEnd() || found->isEmpty())
        return nullptr;
    QList<const EnumInfo*> visible;
    for (const auto& info : found.value()) {
        if (info.decl && info.decl->moduleName == resolvedModuleName && isEnumVisible(*info.decl))
            visible.push_back(&info);
    }
    if (visible.size() == 1)
        return visible.front();
    if (visible.size() > 1 && diagnose)
        error(span, QStringLiteral("enum '%1::%2' is ambiguous").arg(moduleName, name));
    return nullptr;
}

const TypeChecker::EnumInfo* TypeChecker::resolveEnumInPackage(const QString& name,
                                                               const QString& packageName,
                                                               const SourceSpan& span,
                                                               bool diagnose)
{
    auto found = m_enums.constFind(name);
    if (found == m_enums.constEnd() || found->isEmpty())
        return nullptr;
    for (const auto& info : found.value()) {
        if (info.decl && isDeclInCurrentModule(*info.decl, packageName))
            return &info;
    }
    QList<const EnumInfo*> visible;
    QList<const EnumInfo*> hidden;
    for (const auto& info : found.value()) {
        if (!info.decl)
            continue;
        if (isEnumVisible(*info.decl))
            visible.push_back(&info);
        else
            hidden.push_back(&info);
    }
    if (visible.size() == 1)
        return visible.front();
    if (visible.size() > 1 && diagnose)
        error(span, QStringLiteral("enum '%1' is ambiguous across imported modules").arg(name));
    if (visible.isEmpty() && !hidden.isEmpty() && diagnose)
        error(span, QStringLiteral("enum '%1' exists only as non-exported dependency symbols").arg(name));
    return nullptr;
}

const TypeAliasDeclNode* TypeChecker::resolveTypeAlias(const QString& name,
                                                       const QString& packageName,
                                                       const SourceSpan& span,
                                                       bool diagnose)
{
    if (const auto qualified = splitQualifiedSymbol(name))
        return resolveTypeAliasInModule(qualified->first, qualified->second, span, diagnose);
    auto found = m_typeAliases.constFind(name);
    if (found == m_typeAliases.constEnd() || found->isEmpty())
        return nullptr;
    for (const TypeAliasDeclNode* alias : found.value()) {
        if (alias && isDeclInCurrentModule(*alias, packageName))
            return alias;
    }
    QList<const TypeAliasDeclNode*> visible;
    QList<const TypeAliasDeclNode*> hidden;
    for (const TypeAliasDeclNode* alias : found.value()) {
        if (!alias)
            continue;
        if (isTypeAliasVisible(*alias))
            visible.push_back(alias);
        else
            hidden.push_back(alias);
    }
    if (visible.size() == 1)
        return visible.front();
    if (visible.size() > 1 && diagnose)
        error(span, QStringLiteral("type alias '%1' is ambiguous across imported modules").arg(name));
    if (visible.isEmpty() && !hidden.isEmpty() && diagnose)
        error(span, QStringLiteral("type alias '%1' exists only as non-exported dependency symbols").arg(name));
    return nullptr;
}

const TypeAliasDeclNode* TypeChecker::resolveTypeAliasInModule(const QString& moduleName,
                                                               const QString& name,
                                                               const SourceSpan& span,
                                                               bool diagnose)
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
    if (visible.size() == 1)
        return visible.front();
    if (visible.size() > 1 && diagnose)
        error(span, QStringLiteral("type alias '%1::%2' is ambiguous").arg(moduleName, name));
    return nullptr;
}

const TypeChecker::BackendInfo* TypeChecker::resolveBackend(const QString& name, const SourceSpan& span, bool diagnose)
{
    return resolveBackendInPackage(name, m_currentPackage, span, diagnose);
}

const TypeChecker::BackendInfo* TypeChecker::resolveBackendInModule(const QString& moduleName,
                                                                    const QString& name,
                                                                    const SourceSpan& span,
                                                                    bool diagnose)
{
    const QString resolvedModuleName = resolveModuleName(moduleName);
    auto found = m_backends.constFind(name);
    if (found == m_backends.constEnd() || found->isEmpty())
        return nullptr;
    QList<const BackendInfo*> visible;
    QList<const BackendInfo*> hidden;
    for (const auto& info : found.value()) {
        if (!info.decl || info.decl->moduleName != resolvedModuleName)
            continue;
        if (isBackendVisible(*info.decl))
            visible.push_back(&info);
        else
            hidden.push_back(&info);
    }
    if (visible.size() == 1)
        return visible.front();
    if (visible.size() > 1) {
        if (diagnose) {
            QList<const BackendBlockNode*> decls;
            for (const BackendInfo* info : visible)
                decls.push_back(info->decl);
            const QStringList origins = declOriginList(decls, [](const BackendBlockNode& decl) { return decl.name; });
            errorDetailed(span,
                          QStringLiteral("backend '%1::%2' is ambiguous").arg(moduleName, name),
                          declSpanList(decls),
                          ambiguityExplanation(QStringLiteral("backend"), origins),
                          qualifySuggestions(QStringLiteral("backend"), origins));
        }
        return nullptr;
    }
    if (hidden.size() == 1) {
        if (diagnose)
            requireBackendVisible(*hidden.front()->decl, span);
        return nullptr;
    }
    return nullptr;
}

const TypeChecker::BackendInfo* TypeChecker::resolveBackendInPackage(const QString& name,
                                                                     const QString& packageName,
                                                                     const SourceSpan& span,
                                                                     bool diagnose)
{
    auto found = m_backends.constFind(name);
    if (found == m_backends.constEnd() || found->isEmpty())
        return nullptr;
    const auto& candidates = found.value();

    for (const auto& info : candidates) {
        if (info.decl && isDeclInCurrentModule(*info.decl, packageName))
            return &info;
    }
    QList<const BackendInfo*> visible;
    QList<const BackendInfo*> hidden;
    for (const auto& info : candidates) {
        if (!info.decl)
            continue;
        if (isBackendVisible(*info.decl))
            visible.push_back(&info);
        else
            hidden.push_back(&info);
    }
    if (visible.size() == 1)
        return visible.front();
    if (visible.size() > 1) {
        if (diagnose) {
            QList<const BackendBlockNode*> decls;
            for (const BackendInfo* info : visible)
                decls.push_back(info->decl);
            const QStringList origins = declOriginList(decls, [](const BackendBlockNode& decl) { return decl.name; });
            errorDetailed(span,
                          QStringLiteral("backend '%1' is ambiguous across imported modules").arg(name),
                          declSpanList(decls),
                          ambiguityExplanation(QStringLiteral("backend"), origins),
                          qualifySuggestions(QStringLiteral("backend"), origins));
        }
        return nullptr;
    }
    if (hidden.size() == 1) {
        if (diagnose)
            requireBackendVisible(*hidden.front()->decl, span);
        return nullptr;
    }
    if (!hidden.isEmpty() && diagnose)
        error(span, QStringLiteral("backend '%1' exists only as non-exported dependency symbols").arg(name));
    return nullptr;
}

AbelType TypeChecker::typeFromAstInCurrentPackage(const TypeNode& node)
{
    return typeFromAstInPackage(node, m_currentPackage);
}

AbelType TypeChecker::typeFromAstInPackage(const TypeNode& node, const QString& packageName, bool diagnose)
{
    if (node.name == QStringLiteral("vector") && node.elementType) {
        AbelType base = makeVectorType(typeFromAstInPackage(*node.elementType, packageName, diagnose));
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
            params.push_back(typeFromAstInPackage(*param, packageName, diagnose));
        AbelType base = makeFunctionType(typeFromAstInPackage(*node.elementType, packageName, diagnose), std::move(params));
        if (node.isConst)
            base = makeConstType(base);
        for (int i = 0; i < node.pointerDepth; ++i)
            base = makePointerType(base);
        if (node.isReference)
            base = makeReferenceType(base);
        return base;
    }

    if (const TypeAliasDeclNode* alias = resolveTypeAlias(node.name, packageName, node.span, diagnose)) {
        const QString key = declarationQualifiedName(*alias, alias->name);
        if (m_resolvingTypeAliases.contains(key)) {
            if (diagnose)
                error(node.span, QStringLiteral("recursive type alias '%1'").arg(alias->name));
            return makeType(TypeKind::Unknown);
        }
        m_resolvingTypeAliases.insert(key);
        AbelType base = typeFromAstForDecl(*alias->targetType, *alias, diagnose);
        m_resolvingTypeAliases.remove(key);
        if (node.isConst)
            base = makeConstType(base);
        for (int i = 0; i < node.pointerDepth; ++i)
            base = makePointerType(base);
        if (node.isReference)
            base = makeReferenceType(base);
        return base;
    }

    const EnumInfo* enumInfo = nullptr;
    if (const auto qualified = splitQualifiedSymbol(node.name))
        enumInfo = resolveEnumInModule(qualified->first, qualified->second, node.span, false);
    else
        enumInfo = resolveEnumInPackage(node.name, packageName, node.span, false);
    if (const EnumInfo* info = enumInfo) {
        AbelType base = makeType(TypeKind::I32, info->decl ? info->decl->name : node.name);
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
            if (const StructInfo* info = resolveStructInModule(qualified->first, qualified->second, node.span, diagnose))
                base = makeStructType(structTypeName(*info->decl));
        } else if (const StructInfo* info = resolveStructInPackage(node.name, packageName, node.span, diagnose)) {
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

AbelType TypeChecker::typeFromAstForDecl(const TypeNode& node, const DeclNode& decl, bool diagnose)
{
    DeclContextGuard context(m_currentPackage, m_currentModule, m_currentImports, m_currentImportAliases, decl);
    return typeFromAstInPackage(node, decl.packageName, diagnose);
}

void TypeChecker::collectBackends(const ProgramNode& program)
{
    for (const auto& decl : program.declarations) {
        auto* backend = dynamic_cast<BackendBlockNode*>(decl.get());
        if (!backend)
            continue;
        const auto existing = m_backends.value(backend->name);
        bool duplicateInModule = false;
        for (const auto& other : existing) {
            if (other.decl && sameDeclNamespace(*other.decl, *backend)) {
                error(backend->span, QStringLiteral("duplicate backend '%1' in package '%2' module '%3'")
                                        .arg(backend->name, backend->packageName, backend->moduleName));
                duplicateInModule = true;
                break;
            }
        }
        if (duplicateInModule)
            continue;
        BackendInfo info;
        info.decl = backend;
        for (const auto& fn : backend->functions) {
            if (info.functions.contains(fn->name)) {
                error(fn->span, QStringLiteral("duplicate backend function '%1::%2'").arg(backend->name, fn->name));
                continue;
            }
            info.functions.insert(fn->name, fn.get());
        }
        m_backends[backend->name].push_back(std::move(info));
    }
}

void TypeChecker::checkTypeAlias(const TypeAliasDeclNode& alias)
{
    DeclContextGuard context(m_currentPackage, m_currentModule, m_currentImports, m_currentImportAliases, alias);
    const AbelType target = typeFromAstInCurrentPackage(*alias.targetType);
    if (!isSupportedType(target))
        error(alias.span, QStringLiteral("unsupported type alias target '%1'").arg(alias.targetType->displayName()));
}

void TypeChecker::checkStruct(const StructDeclNode& decl)
{
    DeclContextGuard context(m_currentPackage, m_currentModule, m_currentImports, m_currentImportAliases, decl);
    for (const auto& field : decl.fields) {
        AbelType type = typeFromAstInCurrentPackage(*field->type);
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
    DeclContextGuard context(m_currentPackage, m_currentModule, m_currentImports, m_currentImportAliases, backend);
    for (const auto& fn : backend.functions) {
        const AbelType returnType = typeFromAstInCurrentPackage(*fn->returnType);
        if (!isSupportedType(returnType))
            error(fn->returnType->span, QStringLiteral("unsupported backend return type '%1'").arg(fn->returnType->displayName()));
        bool seenVariadic = false;
        for (size_t i = 0; i < fn->params.size(); ++i) {
            const ParameterNode& param = *fn->params[i];
            const AbelType paramType = typeFromAstInCurrentPackage(*param.type);
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
    m_currentStruct = structTypeName(owner);
    pushScope();
    defineVariable(QStringLiteral("this"), makePointerType(makeStructType(structTypeName(owner))), false, owner.span);
    for (const auto& field : owner.fields)
        defineVariable(field->name, typeFromAstInCurrentPackage(*field->type), field->type->isConst, field->span);
    for (const auto& param : ctor.params) {
        AbelType paramType = typeFromAstInCurrentPackage(*param->type);
        if (!isSupportedType(paramType))
            error(param->span, QStringLiteral("unsupported parameter type '%1'").arg(param->type->displayName()));
        defineVariable(param->name,
                       param->variadic ? makeVectorType(makeType(TypeKind::Any)) : paramType,
                       !param->variadic && isReadOnlyBinding(paramType, param->type->isConst),
                       param->span);
    }
    checkBlock(*ctor.body, false);
    popScope();
    m_currentStruct.clear();
}

void TypeChecker::checkMethod(const StructDeclNode& owner, const FunctionDeclNode& method)
{
    const qsizetype diagnosticsBeforeCallable = m_diagnostics.size();
    const AbelType returnType = typeFromAstInCurrentPackage(*method.returnType);
    if (!isSupportedType(returnType)) {
        error(method.returnType->span, QStringLiteral("unsupported return type '%1'").arg(method.returnType->displayName()));
        return;
    }
    m_currentReturnType = returnType;
    m_currentStruct = structTypeName(owner);
    pushScope();
    defineVariable(QStringLiteral("this"), makePointerType(makeStructType(structTypeName(owner))), method.isConstMethod, method.span);
    for (const auto& field : owner.fields)
        defineVariable(field->name, typeFromAstInCurrentPackage(*field->type), method.isConstMethod || field->type->isConst, field->span);
    for (const auto& param : method.params) {
        AbelType paramType = typeFromAstInCurrentPackage(*param->type);
        if (!isSupportedType(paramType))
            error(param->span, QStringLiteral("unsupported parameter type '%1'").arg(param->type->displayName()));
        defineVariable(param->name,
                       param->variadic ? makeVectorType(makeType(TypeKind::Any)) : paramType,
                       !param->variadic && isReadOnlyBinding(paramType, param->type->isConst),
                       param->span);
    }
    if (method.body)
        checkBlock(*method.body, false);
    if (returnType.kind != TypeKind::Void
        && method.body
        && m_diagnostics.size() == diagnosticsBeforeCallable
        && !blockAlwaysReturns(*method.body)) {
        error(method.span,
              QStringLiteral("method '%1' may end without returning %2")
                  .arg(method.name, returnType.displayName()));
    }
    popScope();
    m_currentStruct.clear();
}

void TypeChecker::checkFunction(const FunctionDeclNode& fn)
{
    DeclContextGuard context(m_currentPackage, m_currentModule, m_currentImports, m_currentImportAliases, fn);
    const qsizetype diagnosticsBeforeCallable = m_diagnostics.size();
    const AbelType returnType = typeFromAstInCurrentPackage(*fn.returnType);
    if (!isSupportedType(returnType)) {
        error(fn.returnType->span, QStringLiteral("unsupported return type '%1'").arg(fn.returnType->displayName()));
        return;
    }
    m_currentReturnType = returnType;

    if (fn.isOperator) {
        if (fn.operatorSymbol.isEmpty())
            error(fn.span, QStringLiteral("operator overload is missing an operator symbol"));
        if (fn.debt)
            error(fn.span, QStringLiteral("operator '%1' overload cannot be debt in this slice").arg(fn.operatorSymbol));
        if (fn.params.size() != 2)
            error(fn.span, QStringLiteral("operator '%1' overload must take exactly two parameters").arg(fn.operatorSymbol));
    }

    pushScope();
    bool seenVariadic = false;
    for (size_t i = 0; i < fn.params.size(); ++i) {
        const ParameterNode& param = *fn.params[i];
        const AbelType paramType = typeFromAstInCurrentPackage(*param.type);
        if (!isSupportedType(paramType)) {
            error(param.span, QStringLiteral("unsupported parameter type '%1'").arg(param.type->displayName()));
            continue;
        }
        if (fn.isOperator && param.variadic)
            error(param.span, QStringLiteral("operator '%1' overload cannot be variadic").arg(fn.operatorSymbol));
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
            if (fn.isOperator && paramType.isReference() && !isConstReferenceType(paramType)) {
                error(param.span,
                      QStringLiteral("operator '%1' overload parameters cannot be mutable references in this slice")
                          .arg(fn.operatorSymbol));
            }
            defineVariable(param.name, paramType, isReadOnlyBinding(paramType, param.type->isConst), param.span);
        }
    }

    if (!fn.debt && fn.body)
        checkBlock(*fn.body, false);
    if (!fn.debt
        && returnType.kind != TypeKind::Void
        && fn.body
        && m_diagnostics.size() == diagnosticsBeforeCallable
        && !blockAlwaysReturns(*fn.body)) {
        error(fn.span,
              QStringLiteral("function '%1' may end without returning %2")
                  .arg(fn.name, returnType.displayName()));
    }
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
    const AbelType type = typeFromAstInCurrentPackage(*stmt.type);
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
        else if (!isConstReferenceType(type) && !init.isMutable)
            error(stmt.init->span, QStringLiteral("non-const reference variable '%1' cannot bind to const lvalue").arg(stmt.name));
        else if (type.pointee) {
            AbelType referred = *type.pointee;
            referred.isConst = false;
            if (!isAssignable(referred, init.type))
                error(stmt.init->span,
                      QStringLiteral("cannot bind %1 to %2").arg(type.displayName(), init.type.displayName()));
        } else {
            error(stmt.init->span, QStringLiteral("cannot bind %1").arg(type.displayName()));
        }
        defineVariable(stmt.name, type, isReadOnlyBinding(type, stmt.isConst || stmt.type->isConst), stmt.span);
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
    } else if (!isDefaultConstructionAccessible(type)) {
        error(stmt.span, QStringLiteral("default constructor for %1 is private").arg(type.displayName()));
    }
    defineVariable(stmt.name, type, isReadOnlyBinding(type, stmt.isConst || stmt.type->isConst), stmt.span);
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
    defineVariable(stmt.variable,
                   makeReferenceType(*range.type.pointee),
                   !range.isMutable || range.type.pointee->isConst,
                   stmt.span);
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
    if (!var) {
        const int dot = expr.name.indexOf(QLatin1Char('.'));
        if (dot > 0) {
            const QString enumName = expr.name.left(dot);
            const QString enumerator = expr.name.mid(dot + 1);
            if (const EnumInfo* info = resolveEnum(enumName, expr.span, true)) {
                if (info->values.contains(enumerator))
                    return {makeType(TypeKind::I32, enumName), ValueCategory::PRValue, false};
                return errorExpr(expr.span, QStringLiteral("enum '%1' has no enumerator '%2'").arg(enumName, enumerator));
            }
        }
        return errorExpr(expr.span, QStringLiteral("unknown variable '%1'").arg(expr.name));
    }
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
        AbelType pointee = inner.type;
        if (!inner.isMutable)
            pointee = makeConstType(pointee);
        return {makePointerType(pointee), ValueCategory::PRValue, false};
    }
    if (expr.op == QStringLiteral("*")) {
        ExprType inner = checkExpr(*expr.expr);
        if (isUnknownType(inner.type))
            return unknownExprType();
        if (!inner.type.isPointer() || !inner.type.pointee)
            return errorExpr(expr.span, QStringLiteral("dereference requires pointer"));
        return {*inner.type.pointee, ValueCategory::LValue, !inner.type.pointee->isConst};
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
        const bool ok = isBuiltinEqualityComparable(lhs.type, rhs.type);
        if (!ok) {
            return checkUserBinaryOperator(
                op,
                lhs,
                rhs,
                expr.span,
                QStringLiteral("cannot compare %1 and %2").arg(lhs.type.displayName(), rhs.type.displayName()));
        }
        return {makeType(TypeKind::Bool), ValueCategory::PRValue, false};
    }
    if (op == QStringLiteral("+") && lhs.type.kind == TypeKind::Str && rhs.type.kind == TypeKind::Str)
        return {makeType(TypeKind::Str), ValueCategory::PRValue, false};

    if (!lhs.type.isNumeric() || !rhs.type.isNumeric())
        return checkUserBinaryOperator(
            op,
            lhs,
            rhs,
            expr.span,
            QStringLiteral("operator '%1' requires numeric operands").arg(op));

    if (op == QStringLiteral("<") || op == QStringLiteral("<=") || op == QStringLiteral(">") || op == QStringLiteral(">="))
        return {makeType(TypeKind::Bool), ValueCategory::PRValue, false};
    if (op == QStringLiteral("/") || op == QStringLiteral("*") || op == QStringLiteral("+") || op == QStringLiteral("-")
        || op == QStringLiteral("%") || op == QStringLiteral("%%") || op == QStringLiteral("**")
        || op == QStringLiteral("<?") || op == QStringLiteral(">?")) {
        return {numericBinaryResultType(lhs.type, rhs.type), ValueCategory::PRValue, false};
    }

    return checkUserBinaryOperator(
        op,
        lhs,
        rhs,
        expr.span,
        QStringLiteral("unknown binary operator '%1'").arg(op));
}

ExprType TypeChecker::checkUserBinaryOperator(const QString& op,
                                              const ExprType& lhs,
                                              const ExprType& rhs,
                                              const SourceSpan& span,
                                              const QString& fallbackMessage)
{
    const QString name = QStringLiteral("operator ") + op;
    const QList<const FunctionDeclNode*> candidates = resolveFunctionCandidates(name, span, false);
    if (candidates.isEmpty())
        return errorExpr(span, fallbackMessage);

    QList<const FunctionDeclNode*> matches;
    QList<const FunctionDeclNode*> considered;
    int bestScore = 1'000'000;
    for (const FunctionDeclNode* fn : candidates) {
        if (!fn || !fn->isOperator || fn->operatorSymbol != op)
            continue;
        considered.push_back(fn);
        if (fn->params.size() != 2 || (!fn->params.empty() && fn->params.back()->variadic))
            continue;
        const AbelType lhsParam = typeFromAstForDecl(*fn->params[0]->type, *fn);
        const AbelType rhsParam = typeFromAstForDecl(*fn->params[1]->type, *fn);
        const auto lhsScore = scoreParameterArgument(lhsParam, lhs);
        const auto rhsScore = scoreParameterArgument(rhsParam, rhs);
        if (!lhsScore || !rhsScore)
            continue;
        const int score = *lhsScore + *rhsScore;
        if (score < bestScore) {
            bestScore = score;
            matches.clear();
            matches.push_back(fn);
        } else if (score == bestScore) {
            matches.push_back(fn);
        }
    }

    if (considered.isEmpty())
        return errorExpr(span, QStringLiteral("function '%1' is not a valid operator overload").arg(name));
    if (matches.isEmpty()) {
        return errorExpr(span,
                         QStringLiteral("no matching operator '%1' overload for %2 and %3")
                             .arg(op, lhs.type.displayName(), rhs.type.displayName()));
    }
    if (matches.size() > 1) {
        const QStringList origins = declOriginList(matches, [](const FunctionDeclNode& fn) { return fn.name; });
        return errorExpr(span,
                         QStringLiteral("operator '%1' is ambiguous for %2 and %3; candidates: %4")
                             .arg(op,
                                  lhs.type.displayName(),
                                  rhs.type.displayName(),
                                  origins.join(QStringLiteral(", "))));
    }

    const FunctionDeclNode* fn = matches.front();
    return callReturnExprType(typeFromAstForDecl(*fn->returnType, *fn));
}

ExprType TypeChecker::checkCast(const CastExprNode& expr)
{
        const AbelType target = typeFromAstInCurrentPackage(*expr.targetType);
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

    if (m_functions.contains(name)) {
        std::vector<ExprType> checked;
        std::vector<SourceSpan> spans;
        checked.reserve(args.size() + 1);
        spans.reserve(args.size() + 1);
        checked.push_back(lhs);
        spans.push_back(span);
        for (const auto& argExpr : args) {
            checked.push_back(checkExpr(*argExpr));
            spans.push_back(argExpr->span);
        }
        const QList<const FunctionDeclNode*> candidates = resolveFunctionCandidates(name, nameSpan);
        if (candidates.isEmpty())
            return unknownExprType();
        return checkFunctionOverloadCall(name, candidates, checked, spans, span);
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
        if (name == QStringLiteral("scan")) {
            if (!lhs.type.isPointer())
                error(span, QStringLiteral("scan argument must be pointer, got %1").arg(lhs.type.displayName()));
            else if (lhs.type.pointee && lhs.type.pointee->isConst)
                error(span, QStringLiteral("scan cannot write through pointer to const %1").arg(lhs.type.pointee->displayName()));
            else if (lhs.type.pointee && !isScannableType(*lhs.type.pointee))
                error(span, QStringLiteral("scan does not support target type %1").arg(lhs.type.pointee->displayName()));
            for (const auto& argExpr : args) {
                ExprType arg = checkExpr(*argExpr);
                if (!isUnknownType(arg.type)) {
                    if (!arg.type.isPointer())
                        error(argExpr->span, QStringLiteral("scan argument must be pointer, got %1").arg(arg.type.displayName()));
                    else if (arg.type.pointee && arg.type.pointee->isConst)
                        error(argExpr->span, QStringLiteral("scan cannot write through pointer to const %1").arg(arg.type.pointee->displayName()));
                    else if (arg.type.pointee && !isScannableType(*arg.type.pointee))
                        error(argExpr->span, QStringLiteral("scan does not support target type %1").arg(arg.type.pointee->displayName()));
                }
            }
            return {makeType(TypeKind::Void), ValueCategory::PRValue, false};
        }
        if (isCharBuiltinName(name)) {
            if (argc != 1)
                return errorExpr(span, QStringLiteral("%1 expects one argument").arg(name));
            if (name == QStringLiteral("char_from_code")) {
                if (!lhs.type.isInteger())
                    return errorExpr(span, QStringLiteral("char_from_code expects integer, got %1").arg(lhs.type.displayName()));
            } else if (lhs.type.kind != TypeKind::Char) {
                return errorExpr(span, QStringLiteral("%1 expects char, got %2").arg(name, lhs.type.displayName()));
            }
            return {charBuiltinReturnType(name), ValueCategory::PRValue, false};
        }
        if (isAnyBuiltinName(name)) {
            const qsizetype expected = anyBuiltinArity(name);
            if (argc != expected)
                return errorExpr(span, QStringLiteral("%1 expects %2 argument(s)").arg(name).arg(expected));
            bool hasUnknown = false;
            if (lhs.type.kind != TypeKind::Any)
                error(span, QStringLiteral("%1 argument 1 expects any, got %2").arg(name, lhs.type.displayName()));
            if (name == QStringLiteral("any_is")) {
                ExprType expectedName = checkExpr(*args[0]);
                hasUnknown = hasUnknown || isUnknownType(expectedName.type);
                if (!isUnknownType(expectedName.type) && expectedName.type.kind != TypeKind::Str)
                    error(args[0]->span, QStringLiteral("any_is argument 2 expects str, got %1").arg(expectedName.type.displayName()));
            }
            if (hasUnknown)
                return unknownExprType();
            return {anyBuiltinReturnType(name), ValueCategory::PRValue, false};
        }
        if (isFilePathBuiltinName(name)) {
            const qsizetype expected = filePathBuiltinArity(name);
            if (argc != expected)
                return errorExpr(span, QStringLiteral("%1 expects %2 argument(s)").arg(name).arg(expected));
            bool hasUnknown = isUnknownType(lhs.type);
            if (!hasUnknown) {
                auto expectedType = filePathBuiltinArgType(name, 0);
                if (expectedType.has_value() && !isAssignable(*expectedType, lhs.type))
                    error(span, QStringLiteral("%1 argument 1 expects %2, got %3")
                                    .arg(name, expectedType->displayName(), lhs.type.displayName()));
            }
            for (size_t i = 0; i < args.size(); ++i) {
                ExprType arg = checkExpr(*args[i]);
                hasUnknown = hasUnknown || isUnknownType(arg.type);
                auto expectedType = filePathBuiltinArgType(name, static_cast<qsizetype>(i + 1));
                if (!isUnknownType(arg.type) && expectedType.has_value() && !isAssignable(*expectedType, arg.type)) {
                    error(args[i]->span,
                          QStringLiteral("%1 argument %2 expects %3, got %4")
                              .arg(name)
                              .arg(i + 2)
                              .arg(expectedType->displayName(), arg.type.displayName()));
                }
            }
            if (hasUnknown)
                return unknownExprType();
            return {filePathBuiltinReturnType(name), ValueCategory::PRValue, false};
        }
        if (isMathBuiltinName(name)) {
            const qsizetype expected = mathBuiltinArity(name);
            if (argc != expected)
                return errorExpr(span, QStringLiteral("%1 expects %2 argument(s)").arg(name).arg(expected));
            std::vector<ExprType> checked;
            checked.reserve(args.size() + 1);
            checked.push_back(lhs);
            bool hasUnknown = isUnknownType(lhs.type);
            bool hasError = false;
            if (!hasUnknown && mathBuiltinRequiresInteger(name) && !lhs.type.isInteger()) {
                error(span, QStringLiteral("%1 expects integer argument, got %2").arg(name, lhs.type.displayName()));
                hasError = true;
            } else if (!hasUnknown && !mathBuiltinRequiresInteger(name) && !lhs.type.isNumeric()) {
                error(span, QStringLiteral("%1 expects numeric argument, got %2").arg(name, lhs.type.displayName()));
                hasError = true;
            }
            for (const auto& argExpr : args) {
                ExprType arg = checkExpr(*argExpr);
                hasUnknown = hasUnknown || isUnknownType(arg.type);
                if (!isUnknownType(arg.type) && mathBuiltinRequiresInteger(name) && !arg.type.isInteger()) {
                    error(argExpr->span, QStringLiteral("%1 expects integer argument, got %2").arg(name, arg.type.displayName()));
                    hasError = true;
                } else if (!isUnknownType(arg.type) && !mathBuiltinRequiresInteger(name) && !arg.type.isNumeric()) {
                    error(argExpr->span, QStringLiteral("%1 expects numeric argument, got %2").arg(name, arg.type.displayName()));
                    hasError = true;
                }
                checked.push_back(arg);
            }
            if (hasUnknown || hasError)
                return unknownExprType();
            if (name == QStringLiteral("abs"))
                return {checked[0].type, ValueCategory::PRValue, false};
            if (mathBuiltinReturnsF64(name))
                return {makeType(TypeKind::F64), ValueCategory::PRValue, false};
            if (mathBuiltinRequiresInteger(name))
                return {numericBinaryResultType(checked[0].type, checked[1].type), ValueCategory::PRValue, false};
            if (name == QStringLiteral("clamp")) {
                AbelType out = numericBinaryResultType(checked[0].type, checked[1].type);
                out = numericBinaryResultType(out, checked[2].type);
                return {out, ValueCategory::PRValue, false};
            }
            return {numericBinaryResultType(checked[0].type, checked[1].type), ValueCategory::PRValue, false};
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
        if (name == QStringLiteral("test_assert")) {
            if (!isUnknownType(lhs.type) && lhs.type.kind != TypeKind::Bool)
                error(span, QStringLiteral("test_assert condition must be bool, got %1").arg(lhs.type.displayName()));
            for (const auto& argExpr : args) {
                ExprType arg = checkExpr(*argExpr);
                if (!isUnknownType(arg.type) && !isStringifiable(arg.type))
                    error(argExpr->span, QStringLiteral("cannot stringify %1").arg(arg.type.displayName()));
            }
            return {makeType(TypeKind::Void), ValueCategory::PRValue, false};
        }
        if (name == QStringLiteral("test_eq") || name == QStringLiteral("test_ne")) {
            if (args.empty())
                return errorExpr(span, QStringLiteral("%1 expects at least two arguments").arg(name));
            ExprType rhs = checkExpr(*args[0]);
            if (!isTestComparable(lhs.type, rhs.type))
                error(args[0]->span, QStringLiteral("%1 cannot compare %2 and %3")
                                          .arg(name, lhs.type.displayName(), rhs.type.displayName()));
            if (!isUnknownType(lhs.type) && !isStringifiable(lhs.type))
                error(span, QStringLiteral("cannot stringify %1").arg(lhs.type.displayName()));
            if (!isUnknownType(rhs.type) && !isStringifiable(rhs.type))
                error(args[0]->span, QStringLiteral("cannot stringify %1").arg(rhs.type.displayName()));
            for (size_t i = 1; i < args.size(); ++i) {
                ExprType arg = checkExpr(*args[i]);
                if (!isUnknownType(arg.type) && !isStringifiable(arg.type))
                    error(args[i]->span, QStringLiteral("cannot stringify %1").arg(arg.type.displayName()));
            }
            return {makeType(TypeKind::Void), ValueCategory::PRValue, false};
        }
        if (name == QStringLiteral("test_close")) {
            if (args.size() < 2)
                return errorExpr(span, QStringLiteral("test_close expects at least three arguments"));
            ExprType expected = checkExpr(*args[0]);
            ExprType eps = checkExpr(*args[1]);
            if (!isUnknownType(lhs.type) && !lhs.type.isNumeric())
                error(span, QStringLiteral("test_close actual must be numeric, got %1").arg(lhs.type.displayName()));
            if (!isUnknownType(expected.type) && !expected.type.isNumeric())
                error(args[0]->span, QStringLiteral("test_close expected must be numeric, got %1").arg(expected.type.displayName()));
            if (!isUnknownType(eps.type) && !eps.type.isNumeric())
                error(args[1]->span, QStringLiteral("test_close eps must be numeric, got %1").arg(eps.type.displayName()));
            if (!isUnknownType(lhs.type) && !isStringifiable(lhs.type))
                error(span, QStringLiteral("cannot stringify %1").arg(lhs.type.displayName()));
            if (!isUnknownType(expected.type) && !isStringifiable(expected.type))
                error(args[0]->span, QStringLiteral("cannot stringify %1").arg(expected.type.displayName()));
            for (size_t i = 2; i < args.size(); ++i) {
                ExprType arg = checkExpr(*args[i]);
                if (!isUnknownType(arg.type) && !isStringifiable(arg.type))
                    error(args[i]->span, QStringLiteral("cannot stringify %1").arg(arg.type.displayName()));
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
        const AbelType paramType = typeFromAstForDecl(*param.type, fn);
        checkParameterArgument(paramType,
                               arg,
                               argSpan,
                               QStringLiteral("parameter '%1'").arg(param.name));
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
    return callReturnExprType(typeFromAstForDecl(*fn.returnType, fn));
}

ExprType TypeChecker::checkFunctionOverloadCall(const QString& displayName,
                                                const QList<const FunctionDeclNode*>& candidates,
                                                const std::vector<ExprType>& args,
                                                const std::vector<SourceSpan>& argSpans,
                                                const SourceSpan& span)
{
    Q_ASSERT(args.size() == argSpans.size());
    for (const ExprType& arg : args) {
        if (isUnknownType(arg.type))
            return unknownExprType();
    }

    const FunctionDeclNode* onlyOrdinary = nullptr;
    int ordinaryCount = 0;
    for (const FunctionDeclNode* fn : candidates) {
        if (fn && !fn->isOperator) {
            onlyOrdinary = fn;
            ++ordinaryCount;
        }
    }
    if (ordinaryCount == 1) {
        const FunctionDeclNode& fn = *onlyOrdinary;
        const bool variadic = !fn.params.empty() && fn.params.back()->variadic;
        const size_t fixedCount = variadic ? fn.params.size() - 1 : fn.params.size();
        if ((!variadic && args.size() != fn.params.size()) || (variadic && args.size() < fixedCount))
            return errorExpr(span, QStringLiteral("function '%1' called with wrong argument count").arg(displayName));
        for (size_t i = 0; i < fixedCount; ++i) {
            const ParameterNode& param = *fn.params[i];
            const AbelType paramType = typeFromAstForDecl(*param.type, fn);
            checkParameterArgument(paramType,
                                   args[i],
                                   argSpans[i],
                                   QStringLiteral("parameter '%1'").arg(param.name));
        }
        return callReturnExprType(typeFromAstForDecl(*fn.returnType, fn));
    }

    QList<const FunctionDeclNode*> matches;
    QList<const FunctionDeclNode*> considered;
    int bestScore = 1'000'000;
    for (const FunctionDeclNode* fn : candidates) {
        if (!fn || fn->isOperator)
            continue;
        considered.push_back(fn);
        const bool variadic = !fn->params.empty() && fn->params.back()->variadic;
        const size_t fixedCount = variadic ? fn->params.size() - 1 : fn->params.size();
        if ((!variadic && args.size() != fn->params.size()) || (variadic && args.size() < fixedCount))
            continue;

        int score = variadic ? 4 : 0;
        bool ok = true;
        for (size_t i = 0; i < fixedCount; ++i) {
            const AbelType paramType = typeFromAstForDecl(*fn->params[i]->type, *fn);
            const auto argScore = scoreParameterArgument(paramType, args[i]);
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
            matches.push_back(fn);
        } else if (score == bestScore) {
            matches.push_back(fn);
        }
    }

    if (considered.isEmpty())
        return errorExpr(span, QStringLiteral("function '%1' has no ordinary overloads").arg(displayName));
    if (matches.isEmpty()) {
        return errorExpr(span,
                         QStringLiteral("no matching function '%1' overload for %2 argument(s)")
                             .arg(displayName)
                             .arg(args.size()));
    }
    if (matches.size() > 1) {
        const QStringList origins = declOriginList(matches, [](const FunctionDeclNode& fn) { return fn.name; });
        bool sameSignatureAmbiguity = true;
        for (qsizetype i = 1; i < matches.size(); ++i)
            sameSignatureAmbiguity = sameSignatureAmbiguity && sameFunctionSignature(*matches.front(), *matches[i]);
        errorDetailed(span,
                      sameSignatureAmbiguity
                          ? QStringLiteral("function '%1' is ambiguous across imported modules").arg(displayName)
                          : QStringLiteral("function '%1' overload is ambiguous").arg(displayName),
                      declSpanList(matches),
                      ambiguityExplanation(QStringLiteral("function"), origins),
                      qualifySuggestions(QStringLiteral("function"), origins));
        return unknownExprType();
    }

    const FunctionDeclNode* fn = matches.front();
    const bool variadic = !fn->params.empty() && fn->params.back()->variadic;
    const size_t fixedCount = variadic ? fn->params.size() - 1 : fn->params.size();
    for (size_t i = 0; i < fixedCount; ++i) {
        const ParameterNode& param = *fn->params[i];
        const AbelType paramType = typeFromAstForDecl(*param.type, *fn);
        checkParameterArgument(paramType,
                               args[i],
                               argSpans[i],
                               QStringLiteral("parameter '%1'").arg(param.name));
    }
    return callReturnExprType(typeFromAstForDecl(*fn->returnType, *fn));
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
        checkParameterArgument(paramType, arg, argSpan, QStringLiteral("function parameter"));
    };

    checkOne(0, firstArg, span);
    for (size_t i = 1; i < functionType.params.size(); ++i) {
        ExprType arg = checkExpr(*restArgs[i - 1]);
        checkOne(i, arg, restArgs[i - 1]->span);
    }
    return callReturnExprType(*functionType.pointee);
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
        return checkStaticCall(*access, expr.args, expr.span);

    if (auto* field = dynamic_cast<FieldAccessExprNode*>(expr.callee.get())) {
        ExprType receiver = checkExpr(*field->base);
        if (isUnknownType(receiver.type)) {
            for (const auto& argExpr : expr.args)
                checkExpr(*argExpr);
            return unknownExprType();
        }
        if (receiver.type.kind == TypeKind::Struct) {
            const StructInfo* info = structInfoForType(receiver.type);
            if (!info)
                return errorExpr(field->span, QStringLiteral("unknown struct type '%1'").arg(receiver.type.displayName()));
            const FunctionDeclNode* method = info->methods.value(field->field, nullptr);
            if (!method)
                return errorExpr(field->span, QStringLiteral("unknown method '%1' on %2").arg(field->field, receiver.type.displayName()));
            if (method->isPrivate && m_currentStruct != structTypeName(*info->decl))
                return errorExpr(field->span, QStringLiteral("method '%1' is private").arg(field->field));
            if (receiver.category == ValueCategory::LValue && !receiver.isMutable && !method->isConstMethod)
                return errorExpr(field->span, QStringLiteral("method '%1' requires mutable receiver").arg(field->field));
            if (expr.args.size() != method->params.size())
                return errorExpr(expr.span, QStringLiteral("method '%1' called with wrong argument count").arg(field->field));
            for (size_t i = 0; i < expr.args.size(); ++i) {
                AbelType paramType = typeFromAstForDecl(*method->params[i]->type, *method);
                ExprType arg = checkExpr(*expr.args[i]);
                if (!isUnknownType(arg.type))
                    checkParameterArgument(paramType,
                                           arg,
                                           expr.args[i]->span,
                                           QStringLiteral("method parameter '%1'").arg(method->params[i]->name));
            }
            return callReturnExprType(typeFromAstForDecl(*method->returnType, *method));
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

    if (const StructInfo* info = resolveStruct(name->name, name->span)) {
        const size_t argc = expr.args.size();
        if (!info->constructor) {
            if (argc == 0) {
                if (!isDefaultConstructible(makeStructType(structTypeName(*info->decl))))
                    return errorExpr(expr.span, QStringLiteral("constructor '%1' is not default-constructible").arg(name->name));
            } else {
                if (argc != info->fields.size())
                    return errorExpr(expr.span, QStringLiteral("constructor '%1' expects 0 or %2 argument(s)").arg(name->name).arg(info->fields.size()));
                for (size_t i = 0; i < argc; ++i) {
                    const QString& fieldName = info->decl->fields[i]->name;
                    const FieldInfo field = info->fields.value(fieldName);
                    if (field.isPrivate && m_currentStruct != structTypeName(*info->decl)) {
                        error(expr.args[i]->span, QStringLiteral("field '%1' is private").arg(fieldName));
                        continue;
                    }
                    ExprType arg = checkExpr(*expr.args[i]);
                    if (!isUnknownType(arg.type) && !isAssignable(field.type, arg.type))
                        error(expr.args[i]->span, QStringLiteral("cannot initialize field '%1'").arg(fieldName));
                }
            }
        } else {
            if (info->constructor->isPrivate && m_currentStruct != structTypeName(*info->decl))
                return errorExpr(expr.span, QStringLiteral("constructor '%1' is private").arg(name->name));
            if (argc != info->constructor->params.size())
                return errorExpr(expr.span, QStringLiteral("constructor '%1' called with wrong argument count").arg(name->name));
            for (size_t i = 0; i < argc; ++i) {
                AbelType paramType = typeFromAstForDecl(*info->constructor->params[i]->type, *info->decl);
                ExprType arg = checkExpr(*expr.args[i]);
                if (!isUnknownType(arg.type))
                    checkParameterArgument(paramType,
                                           arg,
                                           expr.args[i]->span,
                                           QStringLiteral("constructor parameter '%1'").arg(info->constructor->params[i]->name));
            }
        }
        return {makeStructType(structTypeName(*info->decl)), ValueCategory::PRValue, false};
    }
    if (m_structs.contains(name->name)) {
        for (const auto& argExpr : expr.args)
            checkExpr(*argExpr);
        return unknownExprType();
    }

    if (m_functions.contains(name->name)) {
        std::vector<ExprType> checked;
        std::vector<SourceSpan> spans;
        checked.reserve(expr.args.size());
        spans.reserve(expr.args.size());
        for (const auto& argExpr : expr.args) {
            checked.push_back(checkExpr(*argExpr));
            spans.push_back(argExpr->span);
        }
        const QList<const FunctionDeclNode*> candidates = resolveFunctionCandidates(name->name, name->span);
        if (candidates.isEmpty())
            return unknownExprType();
        return checkFunctionOverloadCall(name->name, candidates, checked, spans, expr.span);
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
        if (name->name == QStringLiteral("scan")) {
            for (const auto& argExpr : expr.args) {
                ExprType arg = checkExpr(*argExpr);
                if (!isUnknownType(arg.type)) {
                    if (!arg.type.isPointer())
                        error(argExpr->span, QStringLiteral("scan argument must be pointer, got %1").arg(arg.type.displayName()));
                    else if (arg.type.pointee && arg.type.pointee->isConst)
                        error(argExpr->span, QStringLiteral("scan cannot write through pointer to const %1").arg(arg.type.pointee->displayName()));
                    else if (arg.type.pointee && !isScannableType(*arg.type.pointee))
                        error(argExpr->span, QStringLiteral("scan does not support target type %1").arg(arg.type.pointee->displayName()));
                }
            }
            return {makeType(TypeKind::Void), ValueCategory::PRValue, false};
        }
        if (isCharBuiltinName(name->name)) {
            if (argc != 1)
                return errorExpr(expr.span, QStringLiteral("%1 expects one argument").arg(name->name));
            ExprType arg = checkExpr(*expr.args[0]);
            if (!isUnknownType(arg.type)) {
                if (name->name == QStringLiteral("char_from_code")) {
                    if (!arg.type.isInteger())
                        return errorExpr(expr.args[0]->span,
                                         QStringLiteral("char_from_code expects integer, got %1").arg(arg.type.displayName()));
                } else if (arg.type.kind != TypeKind::Char) {
                    return errorExpr(expr.args[0]->span,
                                     QStringLiteral("%1 expects char, got %2").arg(name->name, arg.type.displayName()));
                }
            }
            return {charBuiltinReturnType(name->name), ValueCategory::PRValue, false};
        }
        if (isAnyBuiltinName(name->name)) {
            const qsizetype expected = anyBuiltinArity(name->name);
            if (argc != expected)
                return errorExpr(expr.span, QStringLiteral("%1 expects %2 argument(s)").arg(name->name).arg(expected));
            bool hasUnknown = false;
            ExprType value = checkExpr(*expr.args[0]);
            hasUnknown = hasUnknown || isUnknownType(value.type);
            if (!isUnknownType(value.type) && value.type.kind != TypeKind::Any)
                error(expr.args[0]->span,
                      QStringLiteral("%1 argument 1 expects any, got %2").arg(name->name, value.type.displayName()));
            if (name->name == QStringLiteral("any_is")) {
                ExprType expectedName = checkExpr(*expr.args[1]);
                hasUnknown = hasUnknown || isUnknownType(expectedName.type);
                if (!isUnknownType(expectedName.type) && expectedName.type.kind != TypeKind::Str)
                    error(expr.args[1]->span,
                          QStringLiteral("any_is argument 2 expects str, got %1").arg(expectedName.type.displayName()));
            }
            if (hasUnknown)
                return unknownExprType();
            return {anyBuiltinReturnType(name->name), ValueCategory::PRValue, false};
        }
        if (isFilePathBuiltinName(name->name)) {
            const qsizetype expected = filePathBuiltinArity(name->name);
            if (argc != expected)
                return errorExpr(expr.span, QStringLiteral("%1 expects %2 argument(s)").arg(name->name).arg(expected));
            bool hasUnknown = false;
            for (size_t i = 0; i < expr.args.size(); ++i) {
                ExprType arg = checkExpr(*expr.args[i]);
                hasUnknown = hasUnknown || isUnknownType(arg.type);
                auto expectedType = filePathBuiltinArgType(name->name, static_cast<qsizetype>(i));
                if (!isUnknownType(arg.type) && expectedType.has_value() && !isAssignable(*expectedType, arg.type)) {
                    error(expr.args[i]->span,
                          QStringLiteral("%1 argument %2 expects %3, got %4")
                              .arg(name->name)
                              .arg(i + 1)
                              .arg(expectedType->displayName(), arg.type.displayName()));
                }
            }
            if (hasUnknown)
                return unknownExprType();
            return {filePathBuiltinReturnType(name->name), ValueCategory::PRValue, false};
        }
        if (isMathBuiltinName(name->name)) {
            const qsizetype expected = mathBuiltinArity(name->name);
            if (argc != expected)
                return errorExpr(expr.span, QStringLiteral("%1 expects %2 argument(s)").arg(name->name).arg(expected));
            std::vector<ExprType> checked;
            checked.reserve(expr.args.size());
            bool hasUnknown = false;
            bool hasError = false;
            for (const auto& argExpr : expr.args) {
                ExprType arg = checkExpr(*argExpr);
                hasUnknown = hasUnknown || isUnknownType(arg.type);
                if (!isUnknownType(arg.type) && mathBuiltinRequiresInteger(name->name) && !arg.type.isInteger()) {
                    error(argExpr->span, QStringLiteral("%1 expects integer argument, got %2").arg(name->name, arg.type.displayName()));
                    hasError = true;
                } else if (!isUnknownType(arg.type) && !mathBuiltinRequiresInteger(name->name) && !arg.type.isNumeric()) {
                    error(argExpr->span, QStringLiteral("%1 expects numeric argument, got %2").arg(name->name, arg.type.displayName()));
                    hasError = true;
                }
                checked.push_back(arg);
            }
            if (hasUnknown || hasError)
                return unknownExprType();
            if (name->name == QStringLiteral("abs"))
                return {checked[0].type, ValueCategory::PRValue, false};
            if (mathBuiltinReturnsF64(name->name))
                return {makeType(TypeKind::F64), ValueCategory::PRValue, false};
            if (mathBuiltinRequiresInteger(name->name))
                return {numericBinaryResultType(checked[0].type, checked[1].type), ValueCategory::PRValue, false};
            if (name->name == QStringLiteral("clamp")) {
                AbelType out = numericBinaryResultType(checked[0].type, checked[1].type);
                out = numericBinaryResultType(out, checked[2].type);
                return {out, ValueCategory::PRValue, false};
            }
            return {numericBinaryResultType(checked[0].type, checked[1].type), ValueCategory::PRValue, false};
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
        if (name->name == QStringLiteral("test_assert")) {
            if (argc < 1)
                return errorExpr(expr.span, QStringLiteral("test_assert expects at least one argument"));
            ExprType cond = checkExpr(*expr.args[0]);
            if (!isUnknownType(cond.type) && cond.type.kind != TypeKind::Bool)
                error(expr.args[0]->span, QStringLiteral("test_assert condition must be bool, got %1").arg(cond.type.displayName()));
            for (size_t i = 1; i < expr.args.size(); ++i) {
                ExprType arg = checkExpr(*expr.args[i]);
                if (!isUnknownType(arg.type) && !isStringifiable(arg.type))
                    error(expr.args[i]->span, QStringLiteral("cannot stringify %1").arg(arg.type.displayName()));
            }
            return {makeType(TypeKind::Void), ValueCategory::PRValue, false};
        }
        if (name->name == QStringLiteral("test_eq") || name->name == QStringLiteral("test_ne")) {
            if (argc < 2)
                return errorExpr(expr.span, QStringLiteral("%1 expects at least two arguments").arg(name->name));
            ExprType lhs = checkExpr(*expr.args[0]);
            ExprType rhs = checkExpr(*expr.args[1]);
            if (!isTestComparable(lhs.type, rhs.type))
                error(expr.args[1]->span, QStringLiteral("%1 cannot compare %2 and %3")
                                           .arg(name->name, lhs.type.displayName(), rhs.type.displayName()));
            if (!isUnknownType(lhs.type) && !isStringifiable(lhs.type))
                error(expr.args[0]->span, QStringLiteral("cannot stringify %1").arg(lhs.type.displayName()));
            if (!isUnknownType(rhs.type) && !isStringifiable(rhs.type))
                error(expr.args[1]->span, QStringLiteral("cannot stringify %1").arg(rhs.type.displayName()));
            for (size_t i = 2; i < expr.args.size(); ++i) {
                ExprType arg = checkExpr(*expr.args[i]);
                if (!isUnknownType(arg.type) && !isStringifiable(arg.type))
                    error(expr.args[i]->span, QStringLiteral("cannot stringify %1").arg(arg.type.displayName()));
            }
            return {makeType(TypeKind::Void), ValueCategory::PRValue, false};
        }
        if (name->name == QStringLiteral("test_close")) {
            if (argc < 3)
                return errorExpr(expr.span, QStringLiteral("test_close expects at least three arguments"));
            ExprType actual = checkExpr(*expr.args[0]);
            ExprType expected = checkExpr(*expr.args[1]);
            ExprType eps = checkExpr(*expr.args[2]);
            if (!isUnknownType(actual.type) && !actual.type.isNumeric())
                error(expr.args[0]->span, QStringLiteral("test_close actual must be numeric, got %1").arg(actual.type.displayName()));
            if (!isUnknownType(expected.type) && !expected.type.isNumeric())
                error(expr.args[1]->span, QStringLiteral("test_close expected must be numeric, got %1").arg(expected.type.displayName()));
            if (!isUnknownType(eps.type) && !eps.type.isNumeric())
                error(expr.args[2]->span, QStringLiteral("test_close eps must be numeric, got %1").arg(eps.type.displayName()));
            if (!isUnknownType(actual.type) && !isStringifiable(actual.type))
                error(expr.args[0]->span, QStringLiteral("cannot stringify %1").arg(actual.type.displayName()));
            if (!isUnknownType(expected.type) && !isStringifiable(expected.type))
                error(expr.args[1]->span, QStringLiteral("cannot stringify %1").arg(expected.type.displayName()));
            for (size_t i = 3; i < expr.args.size(); ++i) {
                ExprType arg = checkExpr(*expr.args[i]);
                if (!isUnknownType(arg.type) && !isStringifiable(arg.type))
                    error(expr.args[i]->span, QStringLiteral("cannot stringify %1").arg(arg.type.displayName()));
            }
            return {makeType(TypeKind::Void), ValueCategory::PRValue, false};
        }
    }

    return errorExpr(expr.span, QStringLiteral("unknown function '%1'").arg(name->name));
}

ExprType TypeChecker::checkStaticCall(const StaticAccessExprNode& callee,
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
                    return checkBackendCallByName(moduleName + QStringLiteral("::") + backendName,
                                                  nested->span,
                                                  callee.member,
                                                  args,
                                                  span);
            }
        }
    }

    const QString baseName = staticAccessName(*callee.base);
    if (baseName.isEmpty())
        return errorExpr(callee.span, QStringLiteral("static/backend call receiver must be a name"));

    QString moduleName = baseName;
    moduleName.replace(QStringLiteral("::"), QStringLiteral("."));
    const QString resolvedModuleName = resolveModuleName(moduleName);

    const auto structs = m_structs.value(callee.member);
    for (const auto& info : structs) {
        if (info.decl && info.decl->moduleName == resolvedModuleName) {
            const StructInfo* resolved = resolveStructInModule(moduleName, callee.member, callee.base->span);
            if (!resolved) {
                for (const auto& argExpr : args)
                    checkExpr(*argExpr);
                return unknownExprType();
            }
            return checkStructConstructorCall(moduleName + QStringLiteral("::") + callee.member, *resolved, args, span);
        }
    }

    const auto functions = m_functions.value(callee.member);
    for (const FunctionDeclNode* fn : functions) {
        if (fn->moduleName == resolvedModuleName)
            return checkQualifiedFunctionCall(moduleName, callee.base->span, callee.member, args, span);
    }

    return checkBackendCallByName(baseName, callee.base->span, callee.member, args, span);
}

ExprType TypeChecker::checkStructConstructorCall(const QString& displayName,
                                                 const StructInfo& info,
                                                 const std::vector<std::unique_ptr<ExprNode>>& args,
                                                 const SourceSpan& span)
{
    const size_t argc = args.size();
    if (!info.constructor) {
        if (argc == 0) {
            if (!isDefaultConstructible(makeStructType(structTypeName(*info.decl))))
                return errorExpr(span, QStringLiteral("constructor '%1' is not default-constructible").arg(displayName));
        } else {
            if (argc != info.fields.size())
                return errorExpr(span, QStringLiteral("constructor '%1' expects 0 or %2 argument(s)").arg(displayName).arg(info.fields.size()));
            for (size_t i = 0; i < argc; ++i) {
                const QString& fieldName = info.decl->fields[i]->name;
                const FieldInfo field = info.fields.value(fieldName);
                if (field.isPrivate && m_currentStruct != structTypeName(*info.decl)) {
                    error(args[i]->span, QStringLiteral("field '%1' is private").arg(fieldName));
                    continue;
                }
                ExprType arg = checkExpr(*args[i]);
                if (!isUnknownType(arg.type) && !isAssignable(field.type, arg.type))
                    error(args[i]->span, QStringLiteral("cannot initialize field '%1'").arg(fieldName));
            }
        }
    } else {
        if (info.constructor->isPrivate && m_currentStruct != structTypeName(*info.decl))
            return errorExpr(span, QStringLiteral("constructor '%1' is private").arg(displayName));
        if (argc != info.constructor->params.size())
            return errorExpr(span, QStringLiteral("constructor '%1' called with wrong argument count").arg(displayName));
        for (size_t i = 0; i < argc; ++i) {
            AbelType paramType = typeFromAstForDecl(*info.constructor->params[i]->type, *info.decl);
            ExprType arg = checkExpr(*args[i]);
            if (!isUnknownType(arg.type))
                checkParameterArgument(paramType,
                                       arg,
                                       args[i]->span,
                                       QStringLiteral("constructor parameter '%1'").arg(info.constructor->params[i]->name));
        }
    }
    return {makeStructType(structTypeName(*info.decl)), ValueCategory::PRValue, false};
}

ExprType TypeChecker::checkBackendCall(const StaticAccessExprNode& callee,
                                       const std::vector<std::unique_ptr<ExprNode>>& args,
                                       const SourceSpan& span)
{
    auto* backendName = dynamic_cast<NameExprNode*>(callee.base.get());
    if (!backendName)
        return errorExpr(callee.span, QStringLiteral("backend call receiver must be a backend name"));
    return checkBackendCallByName(backendName->name, backendName->span, callee.member, args, span);
}

ExprType TypeChecker::checkBackendCallByName(const QString& backendName,
                                             const SourceSpan& backendSpan,
                                             const QString& member,
                                             const std::vector<std::unique_ptr<ExprNode>>& args,
                                             const SourceSpan& span)
{
    const int qualifiedSep = backendName.lastIndexOf(QStringLiteral("::"));
    const BackendInfo* backend = qualifiedSep >= 0
        ? resolveBackendInModule(backendName.left(qualifiedSep).replace(QStringLiteral("::"), QStringLiteral(".")),
                                 backendName.mid(qualifiedSep + 2),
                                 backendSpan)
        : resolveBackend(backendName, backendSpan);
    if (!backend) {
        const QString simpleBackendName = qualifiedSep >= 0 ? backendName.mid(qualifiedSep + 2) : backendName;
        if (m_backends.contains(simpleBackendName)) {
            for (const auto& argExpr : args)
                checkExpr(*argExpr);
            return unknownExprType();
        }
        return errorExpr(backendSpan, QStringLiteral("unknown backend '%1'").arg(backendName));
    }
    if (!isBackendVisible(*backend->decl)) {
        for (const auto& argExpr : args)
            checkExpr(*argExpr);
        return unknownExprType();
    }
    const FunctionDeclNode* fn = backend->functions.value(member, nullptr);
    if (!fn)
        return errorExpr(backendSpan, QStringLiteral("unknown backend function '%1::%2'").arg(backendName, member));

    const bool variadic = !fn->params.empty() && fn->params.back()->variadic;
    const size_t fixedCount = variadic ? fn->params.size() - 1 : fn->params.size();
    if ((!variadic && args.size() != fn->params.size()) || (variadic && args.size() < fixedCount))
        return errorExpr(span, QStringLiteral("backend function '%1::%2' called with wrong argument count").arg(backendName, member));

    for (size_t i = 0; i < fixedCount; ++i) {
        const ParameterNode& param = *fn->params[i];
        const AbelType paramType = typeFromAstForDecl(*param.type, *fn);
        ExprType arg = checkExpr(*args[i]);
        if (isUnknownType(arg.type))
            continue;
        checkParameterArgument(paramType,
                               arg,
                               args[i]->span,
                               QStringLiteral("backend parameter '%1'").arg(param.name));
    }
    for (size_t i = fixedCount; i < args.size(); ++i)
        checkExpr(*args[i]);

    const AbelType returnType = typeFromAstForDecl(*fn->returnType, *fn);
    return callReturnExprType(returnType);
}

ExprType TypeChecker::checkQualifiedFunctionCall(const QString& moduleName,
                                                 const SourceSpan& moduleSpan,
                                                 const QString& name,
                                                 const std::vector<std::unique_ptr<ExprNode>>& args,
                                                 const SourceSpan& span)
{
    std::vector<ExprType> checked;
    std::vector<SourceSpan> spans;
    checked.reserve(args.size());
    spans.reserve(args.size());
    for (const auto& argExpr : args) {
        checked.push_back(checkExpr(*argExpr));
        spans.push_back(argExpr->span);
    }
    const QList<const FunctionDeclNode*> candidates = resolveFunctionCandidatesInModule(moduleName, name, moduleSpan);
    if (candidates.isEmpty())
        return unknownExprType();
    return checkFunctionOverloadCall(moduleName + QStringLiteral("::") + name, candidates, checked, spans, span);
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
        checkParameterArgument(paramType, arg, args[i]->span, QStringLiteral("function parameter"));
    }
    return callReturnExprType(*functionType.pointee);
}

ExprType TypeChecker::checkLambda(const LambdaExprNode& expr)
{
    const qsizetype diagnosticsBeforeLambda = m_diagnostics.size();
    AbelType returnType = typeFromAstInCurrentPackage(*expr.returnType);
    if (!isSupportedType(returnType))
        error(expr.returnType->span, QStringLiteral("unsupported lambda return type '%1'").arg(expr.returnType->displayName()));
    std::vector<AbelType> params;
    params.reserve(expr.paramTypes.size());
    for (const auto& type : expr.paramTypes) {
        AbelType paramType = typeFromAstInCurrentPackage(*type);
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
        defineVariable(expr.paramNames[static_cast<qsizetype>(i)],
                       params[i],
                       isReadOnlyBinding(params[i], expr.paramTypes[i]->isConst),
                       expr.span);
    checkBlock(*expr.ownedBody, false);
    if (returnType.kind != TypeKind::Void
        && m_diagnostics.size() == diagnosticsBeforeLambda
        && !blockAlwaysReturns(*expr.ownedBody)) {
        error(expr.span,
              QStringLiteral("lambda may end without returning %1")
                  .arg(returnType.displayName()));
    }
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
    {
        if (receiver.type.kind != TypeKind::Str)
            return errorExpr(callee.span,
                             QStringLiteral("builtin method '%1' requires vector or str receiver").arg(callee.field));
    }
    if (!m_builtins.hasMethod(receiver.type, callee.field))
        return errorExpr(callee.span, QStringLiteral("unknown builtin method '%1'").arg(callee.field));

    if (receiver.type.kind == TypeKind::Str) {
        if (callee.field == QStringLiteral("len")) {
            if (!args.empty())
                return errorExpr(callee.span, QStringLiteral("str.len expects no arguments"));
            return {makeType(TypeKind::I32), ValueCategory::PRValue, false};
        }
        if (callee.field == QStringLiteral("empty")) {
            if (!args.empty())
                return errorExpr(callee.span, QStringLiteral("str.empty expects no arguments"));
            return {makeType(TypeKind::Bool), ValueCategory::PRValue, false};
        }
        if (callee.field == QStringLiteral("contains")
            || callee.field == QStringLiteral("find")
            || callee.field == QStringLiteral("starts_with")
            || callee.field == QStringLiteral("ends_with")) {
            if (args.size() != 1)
                return errorExpr(callee.span, QStringLiteral("str.%1 expects one argument").arg(callee.field));
            ExprType needle = checkExpr(*args[0]);
            if (!isUnknownType(needle.type) && !isAssignable(makeType(TypeKind::Str), needle.type))
                error(args[0]->span, QStringLiteral("str.%1 expects str argument").arg(callee.field));
            return callee.field == QStringLiteral("contains")
                    || callee.field == QStringLiteral("starts_with")
                    || callee.field == QStringLiteral("ends_with")
                ? ExprType{makeType(TypeKind::Bool), ValueCategory::PRValue, false}
                : ExprType{makeType(TypeKind::I32), ValueCategory::PRValue, false};
        }
        if (callee.field == QStringLiteral("substr") || callee.field == QStringLiteral("slice")) {
            if (args.size() != 2)
                return errorExpr(callee.span, QStringLiteral("str.%1 expects two arguments").arg(callee.field));
            ExprType start = checkExpr(*args[0]);
            ExprType len = checkExpr(*args[1]);
            if (!isUnknownType(start.type) && !isAssignable(makeType(TypeKind::I64), start.type))
                error(args[0]->span, QStringLiteral("str.%1 start must be integer").arg(callee.field));
            if (!isUnknownType(len.type) && !isAssignable(makeType(TypeKind::I64), len.type))
                error(args[1]->span, QStringLiteral("str.%1 length must be integer").arg(callee.field));
            return {makeType(TypeKind::Str), ValueCategory::PRValue, false};
        }
        if (callee.field == QStringLiteral("replace")) {
            if (args.size() != 2)
                return errorExpr(callee.span, QStringLiteral("str.replace expects two arguments"));
            ExprType before = checkExpr(*args[0]);
            ExprType after = checkExpr(*args[1]);
            if (!isUnknownType(before.type) && !isAssignable(makeType(TypeKind::Str), before.type))
                error(args[0]->span, QStringLiteral("str.replace before argument must be str"));
            if (!isUnknownType(after.type) && !isAssignable(makeType(TypeKind::Str), after.type))
                error(args[1]->span, QStringLiteral("str.replace after argument must be str"));
            return {makeType(TypeKind::Str), ValueCategory::PRValue, false};
        }
        if (callee.field == QStringLiteral("trim")
            || callee.field == QStringLiteral("lower")
            || callee.field == QStringLiteral("upper")) {
            if (!args.empty())
                return errorExpr(callee.span, QStringLiteral("str.%1 expects no arguments").arg(callee.field));
            return {makeType(TypeKind::Str), ValueCategory::PRValue, false};
        }
        if (callee.field == QStringLiteral("split")) {
            if (args.size() != 1)
                return errorExpr(callee.span, QStringLiteral("str.split expects one argument"));
            ExprType sep = checkExpr(*args[0]);
            if (!isUnknownType(sep.type) && !isAssignable(makeType(TypeKind::Str), sep.type))
                error(args[0]->span, QStringLiteral("str.split expects str argument"));
            return {makeVectorType(makeType(TypeKind::Str)), ValueCategory::PRValue, false};
        }
        if (callee.field == QStringLiteral("join")) {
            if (args.size() != 1)
                return errorExpr(callee.span, QStringLiteral("str.join expects one argument"));
            ExprType values = checkExpr(*args[0]);
            const AbelType strVectorType = makeVectorType(makeType(TypeKind::Str));
            if (!isUnknownType(values.type) && !isAssignable(strVectorType, values.type))
                error(args[0]->span, QStringLiteral("str.join expects vector<str> argument"));
            return {makeType(TypeKind::Str), ValueCategory::PRValue, false};
        }
        if (callee.field == QStringLiteral("parse_int")
            || callee.field == QStringLiteral("parse_long")
            || callee.field == QStringLiteral("parse_double")
            || callee.field == QStringLiteral("parse_bool")) {
            if (!args.empty())
                return errorExpr(callee.span, QStringLiteral("str.%1 expects no arguments").arg(callee.field));
            if (callee.field == QStringLiteral("parse_int"))
                return {makeType(TypeKind::I32), ValueCategory::PRValue, false};
            if (callee.field == QStringLiteral("parse_long"))
                return {makeType(TypeKind::I64), ValueCategory::PRValue, false};
            if (callee.field == QStringLiteral("parse_double"))
                return {makeType(TypeKind::F64), ValueCategory::PRValue, false};
            return {makeType(TypeKind::Bool), ValueCategory::PRValue, false};
        }
        return errorExpr(callee.span, QStringLiteral("unsupported builtin method '%1'").arg(callee.field));
    }

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
        if (callee.field == QStringLiteral("resize")) {
            if (!isDefaultConstructible(element))
                error(callee.span, QStringLiteral("vector.resize requires default-constructible element type %1").arg(element.displayName()));
            else if (!isDefaultConstructionAccessible(element))
                error(callee.span, QStringLiteral("vector.resize requires accessible default constructor for %1").arg(element.displayName()));
        }
        return {makeType(TypeKind::Void), ValueCategory::PRValue, false};
    }
    if (callee.field == QStringLiteral("front") || callee.field == QStringLiteral("back")) {
        if (!args.empty())
            return errorExpr(callee.span, QStringLiteral("vector.%1 expects no arguments").arg(callee.field));
        return {element,
                ValueCategory::LValue,
                (receiver.category == ValueCategory::PRValue || receiver.isMutable) && !element.isConst};
    }
    if (callee.field == QStringLiteral("insert")) {
        if (args.size() != 2)
            return errorExpr(callee.span, QStringLiteral("vector.insert expects two arguments"));
        if (receiver.category == ValueCategory::LValue && !receiver.isMutable)
            return errorExpr(callee.span, QStringLiteral("vector.insert requires mutable vector receiver"));
        ExprType index = checkExpr(*args[0]);
        ExprType value = checkExpr(*args[1]);
        if (!isUnknownType(index.type) && !isAssignable(makeType(TypeKind::I64), index.type))
            error(args[0]->span, QStringLiteral("vector.insert index must be integer"));
        if (!isUnknownType(value.type) && !isAssignable(element, value.type))
            error(args[1]->span,
                  QStringLiteral("cannot insert %1 into vector<%2>")
                      .arg(value.type.displayName(), element.displayName()));
        return {makeType(TypeKind::Void), ValueCategory::PRValue, false};
    }
    if (callee.field == QStringLiteral("erase")) {
        if (args.size() != 1)
            return errorExpr(callee.span, QStringLiteral("vector.erase expects one argument"));
        if (receiver.category == ValueCategory::LValue && !receiver.isMutable)
            return errorExpr(callee.span, QStringLiteral("vector.erase requires mutable vector receiver"));
        ExprType index = checkExpr(*args[0]);
        if (!isUnknownType(index.type) && !isAssignable(makeType(TypeKind::I64), index.type))
            error(args[0]->span, QStringLiteral("vector.erase index must be integer"));
        return {element, ValueCategory::PRValue, false};
    }
    if (callee.field == QStringLiteral("find")) {
        if (args.size() != 1)
            return errorExpr(callee.span, QStringLiteral("vector.find expects one argument"));
        ExprType value = checkExpr(*args[0]);
        if (!isUnknownType(value.type) && !isAssignable(element, value.type))
            error(args[0]->span,
                  QStringLiteral("cannot find %1 in vector<%2>")
                      .arg(value.type.displayName(), element.displayName()));
        return {makeType(TypeKind::I32), ValueCategory::PRValue, false};
    }
    if (callee.field == QStringLiteral("contains") || callee.field == QStringLiteral("count")) {
        if (args.size() != 1)
            return errorExpr(callee.span, QStringLiteral("vector.%1 expects one argument").arg(callee.field));
        ExprType value = checkExpr(*args[0]);
        if (!isUnknownType(value.type) && !isAssignable(element, value.type))
            error(args[0]->span,
                  QStringLiteral("cannot search %1 in vector<%2>")
                      .arg(value.type.displayName(), element.displayName()));
        return callee.field == QStringLiteral("contains")
            ? ExprType{makeType(TypeKind::Bool), ValueCategory::PRValue, false}
            : ExprType{makeType(TypeKind::I32), ValueCategory::PRValue, false};
    }
    if (callee.field == QStringLiteral("extend")) {
        if (args.size() != 1)
            return errorExpr(callee.span, QStringLiteral("vector.extend expects one argument"));
        if (receiver.category == ValueCategory::LValue && !receiver.isMutable)
            return errorExpr(callee.span, QStringLiteral("vector.extend requires mutable vector receiver"));
        ExprType other = checkExpr(*args[0]);
        if (!isUnknownType(other.type) && !isAssignable(receiver.type, other.type))
            error(args[0]->span,
                  QStringLiteral("vector.extend expects %1 argument")
                      .arg(receiver.type.displayName()));
        return {makeType(TypeKind::Void), ValueCategory::PRValue, false};
    }
    if (callee.field == QStringLiteral("slice")) {
        if (args.size() != 2)
            return errorExpr(callee.span, QStringLiteral("vector.slice expects two arguments"));
        ExprType start = checkExpr(*args[0]);
        ExprType len = checkExpr(*args[1]);
        if (!isUnknownType(start.type) && !isAssignable(makeType(TypeKind::I64), start.type))
            error(args[0]->span, QStringLiteral("vector.slice start must be integer"));
        if (!isUnknownType(len.type) && !isAssignable(makeType(TypeKind::I64), len.type))
            error(args[1]->span, QStringLiteral("vector.slice length must be integer"));
        return {receiver.type, ValueCategory::PRValue, false};
    }
    if (callee.field == QStringLiteral("sort")) {
        if (!args.empty())
            return errorExpr(callee.span, QStringLiteral("vector.sort expects no arguments"));
        if (receiver.category == ValueCategory::LValue && !receiver.isMutable)
            return errorExpr(callee.span, QStringLiteral("vector.sort requires mutable vector receiver"));
        if (!isBuiltinOrderable(element))
            error(callee.span,
                  QStringLiteral("vector.sort requires orderable element type, got %1")
                      .arg(element.displayName()));
        return {makeType(TypeKind::Void), ValueCategory::PRValue, false};
    }
    if (callee.field == QStringLiteral("reverse") || callee.field == QStringLiteral("unique")) {
        if (!args.empty())
            return errorExpr(callee.span, QStringLiteral("vector.%1 expects no arguments").arg(callee.field));
        if (receiver.category == ValueCategory::LValue && !receiver.isMutable)
            return errorExpr(callee.span, QStringLiteral("vector.%1 requires mutable vector receiver").arg(callee.field));
        return {makeType(TypeKind::Void), ValueCategory::PRValue, false};
    }
    if (callee.field == QStringLiteral("binary_search")
        || callee.field == QStringLiteral("lower_bound")
        || callee.field == QStringLiteral("upper_bound")) {
        if (args.size() != 1)
            return errorExpr(callee.span, QStringLiteral("vector.%1 expects one argument").arg(callee.field));
        ExprType value = checkExpr(*args[0]);
        if (!isUnknownType(value.type) && !isAssignable(element, value.type))
            error(args[0]->span,
                  QStringLiteral("cannot search %1 in vector<%2>")
                      .arg(value.type.displayName(), element.displayName()));
        if (!isBuiltinOrderable(element))
            error(callee.span,
                  QStringLiteral("vector.%1 requires orderable element type, got %2")
                      .arg(callee.field, element.displayName()));
        return callee.field == QStringLiteral("binary_search")
            ? ExprType{makeType(TypeKind::Bool), ValueCategory::PRValue, false}
            : ExprType{makeType(TypeKind::I32), ValueCategory::PRValue, false};
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
    return {*base.type.pointee,
            ValueCategory::LValue,
            (base.category == ValueCategory::PRValue || base.isMutable) && !base.type.pointee->isConst};
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

void TypeChecker::checkParameterArgument(const AbelType& paramType,
                                         const ExprType& arg,
                                         const SourceSpan& argSpan,
                                         const QString& label)
{
    if (isUnknownType(arg.type))
        return;
    if (paramType.isReference()) {
        if (arg.category != ValueCategory::LValue) {
            error(argSpan, QStringLiteral("%1 requires lvalue").arg(label));
            return;
        }
        if (!isConstReferenceType(paramType) && !arg.isMutable) {
            error(argSpan, QStringLiteral("non-const %1 cannot bind to const lvalue").arg(label));
            return;
        }
        if (!paramType.pointee) {
            error(argSpan, QStringLiteral("cannot bind %1").arg(label));
            return;
        }
        AbelType referred = *paramType.pointee;
        referred.isConst = false;
        if (!isAssignable(referred, arg.type))
            error(argSpan,
                  QStringLiteral("cannot bind %1 of type %2 to %3 lvalue")
                      .arg(label, paramType.displayName(), arg.type.displayName()));
        return;
    }
    if (!isAssignable(paramType, arg.type)) {
        error(argSpan,
              QStringLiteral("cannot pass %1 to %2 of type %3")
                  .arg(arg.type.displayName(), label, paramType.displayName()));
    }
}

std::optional<int> TypeChecker::scoreParameterArgument(const AbelType& paramType, const ExprType& arg) const
{
    if (isUnknownType(arg.type))
        return 0;
    if (paramType.isReference()) {
        if (arg.category != ValueCategory::LValue)
            return std::nullopt;
        if (!isConstReferenceType(paramType) && !arg.isMutable)
            return std::nullopt;
        if (!paramType.pointee)
            return std::nullopt;
        AbelType referred = *paramType.pointee;
        referred.isConst = false;
        if (!isAssignable(referred, arg.type))
            return std::nullopt;
        return referred == arg.type ? 1 : 2;
    }
    if (!isAssignable(paramType, arg.type))
        return std::nullopt;
    return paramType == arg.type ? 0 : 1;
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
    if (!expr.pointer) {
        const QString enumName = staticAccessName(*expr.base);
        if (!enumName.isEmpty()) {
            if (dynamic_cast<const NameExprNode*>(expr.base.get()) && lookupVariable(enumName))
                goto normalFieldAccess;
            if (const EnumInfo* info = resolveEnum(enumName, expr.base->span, false)) {
                if (info->values.contains(expr.field))
                    return {makeType(TypeKind::I32, info->decl ? info->decl->name : enumName), ValueCategory::PRValue, false};
                return errorExpr(expr.span, QStringLiteral("enum '%1' has no enumerator '%2'").arg(enumName, expr.field));
            }
        }
    }

normalFieldAccess:
    ExprType base = checkExpr(*expr.base);
    if (isUnknownType(base.type))
        return unknownExprType();
    AbelType objectType = base.type;
    bool mutableBase = base.isMutable && !objectType.isConst;
    if (expr.pointer) {
        if (!objectType.isPointer() || !objectType.pointee)
            return errorExpr(expr.span, QStringLiteral("operator -> requires pointer receiver"));
        objectType = *objectType.pointee;
        mutableBase = !objectType.isConst;
    }
    if (isUnknownType(objectType))
        return unknownExprType();
    if (objectType.kind != TypeKind::Struct)
        return errorExpr(expr.span, QStringLiteral("field access requires struct receiver"));
    const StructInfo* info = structInfoForType(objectType);
    if (!info)
        return errorExpr(expr.span, QStringLiteral("unknown struct type '%1'").arg(objectType.displayName()));
    if (!info->fields.contains(expr.field))
        return errorExpr(expr.span, QStringLiteral("unknown field '%1' on %2").arg(expr.field, objectType.displayName()));
    const FieldInfo field = info->fields.value(expr.field);
    if (field.isPrivate && m_currentStruct != structTypeName(*info->decl))
        return errorExpr(expr.span, QStringLiteral("field '%1' is private").arg(expr.field));
    return {field.type, ValueCategory::LValue, mutableBase && !field.isConst};
}

bool TypeChecker::isSupportedType(const AbelType& type)
{
    if (type.kind == TypeKind::Unknown || type.kind == TypeKind::Nullptr)
        return false;
    if (type.kind == TypeKind::Struct) {
        const StructInfo* info = structInfoForType(type);
        return info && info->decl && isStructVisible(*info->decl);
    }
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

bool TypeChecker::isDeclVisible(const DeclNode& decl) const
{
    return isDeclVisibleInCurrentContext(decl, false);
}

bool TypeChecker::isDeclInCurrentModule(const DeclNode& decl, const QString& packageName) const
{
    const QString package = packageName.isNull() ? m_currentPackage : packageName;
    if (decl.packageName != package)
        return false;
    if (m_currentModule.isEmpty() || decl.moduleName.isEmpty())
        return true;
    return decl.moduleName == m_currentModule;
}

bool TypeChecker::isModuleImported(const QString& moduleName) const
{
    return !moduleName.isEmpty() && m_currentImports.contains(moduleName);
}

QString TypeChecker::resolveModuleName(const QString& moduleName) const
{
    return m_currentImportAliases.value(moduleName, moduleName);
}

bool TypeChecker::isDeclVisibleInCurrentContext(const DeclNode& decl, bool exportedSymbol) const
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

bool TypeChecker::isFunctionVisible(const FunctionDeclNode& fn) const
{
    return isDeclVisibleInCurrentContext(fn, fn.exported);
}

bool TypeChecker::isStructVisible(const StructDeclNode& decl) const
{
    return isDeclVisibleInCurrentContext(decl, decl.exported);
}

bool TypeChecker::isBackendVisible(const BackendBlockNode& backend) const
{
    return isDeclVisibleInCurrentContext(backend, backend.exported);
}

bool TypeChecker::isEnumVisible(const EnumDeclNode& decl) const
{
    return isDeclVisibleInCurrentContext(decl, decl.exported);
}

bool TypeChecker::isTypeAliasVisible(const TypeAliasDeclNode& alias) const
{
    return isDeclVisibleInCurrentContext(alias, alias.exported);
}

bool TypeChecker::requireFunctionVisible(const FunctionDeclNode& fn, const SourceSpan& span)
{
    if (isFunctionVisible(fn))
        return true;
    error(span,
          fn.fromDependency && isModuleImported(fn.moduleName)
              ? QStringLiteral("function '%1' from dependency package '%2' is not exported")
                    .arg(fn.name, fn.packageName)
              : QStringLiteral("function '%1' is not visible from current module").arg(fn.name));
    return false;
}

bool TypeChecker::requireStructVisible(const StructDeclNode& decl, const SourceSpan& span)
{
    if (isStructVisible(decl))
        return true;
    error(span,
          decl.fromDependency && isModuleImported(decl.moduleName)
              ? QStringLiteral("struct '%1' from dependency package '%2' is not exported")
                    .arg(decl.name, decl.packageName)
              : QStringLiteral("struct '%1' is not visible from current module").arg(decl.name));
    return false;
}

bool TypeChecker::requireBackendVisible(const BackendBlockNode& backend, const SourceSpan& span)
{
    if (isBackendVisible(backend))
        return true;
    error(span,
          backend.fromDependency && isModuleImported(backend.moduleName)
              ? QStringLiteral("backend '%1' from dependency package '%2' is not exported")
                    .arg(backend.name, backend.packageName)
              : QStringLiteral("backend '%1' is not visible from current module").arg(backend.name));
    return false;
}

bool TypeChecker::isAssignable(const AbelType& target, const AbelType& source) const
{
    return canAssignValue(target, source);
}

bool TypeChecker::isDefaultConstructible(const AbelType& type)
{
    QSet<QString> visiting;
    return isDefaultConstructible(type, visiting);
}

bool TypeChecker::isDefaultConstructible(const AbelType& type, QSet<QString>& visiting)
{
    switch (type.kind) {
    case TypeKind::Void:
    case TypeKind::Bool:
    case TypeKind::I8:
    case TypeKind::I16:
    case TypeKind::I32:
    case TypeKind::I64:
    case TypeKind::U8:
    case TypeKind::U16:
    case TypeKind::U32:
    case TypeKind::U64:
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
        const StructInfo* info = structInfoForType(type);
        if (!info || !info->decl)
            return false;
        if (visiting.contains(type.spelling))
            return false;
        if (info->constructor)
            return info->constructor->params.empty();

        visiting.insert(type.spelling);
        for (const auto& field : info->decl->fields) {
            if (!isDefaultConstructible(typeFromAstForDecl(*field->type, *info->decl), visiting)) {
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

bool TypeChecker::isDefaultConstructionAccessible(const AbelType& type)
{
    if (type.kind == TypeKind::Vector && type.pointee)
        return isDefaultConstructionAccessible(*type.pointee);
    if (type.kind != TypeKind::Struct)
        return true;
    const StructInfo* info = structInfoForType(type);
    if (!info || !info->decl)
        return false;
    if (!info->constructor)
        return true;
    return !info->constructor->isPrivate || m_currentStruct == structTypeName(*info->decl);
}

bool TypeChecker::isStringifiable(const AbelType& type)
{
    switch (type.kind) {
    case TypeKind::Void:
    case TypeKind::Bool:
    case TypeKind::I8:
    case TypeKind::I16:
    case TypeKind::I32:
    case TypeKind::I64:
    case TypeKind::U8:
    case TypeKind::U16:
    case TypeKind::U32:
    case TypeKind::U64:
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

bool TypeChecker::hasUserToStrFor(const AbelType& type)
{
    if (type.kind != TypeKind::Struct)
        return false;
    const auto candidates = m_functions.value(QStringLiteral("to_str"));
    for (const FunctionDeclNode* fn : candidates) {
        if (!isFunctionVisible(*fn) || fn->params.size() != 1 || fn->params.front()->variadic)
            continue;
        const AbelType returnType = typeFromAstForDecl(*fn->returnType, *fn);
        if (returnType.kind != TypeKind::Str)
            continue;
        const AbelType paramType = typeFromAstForDecl(*fn->params.front()->type, *fn);
        if (isAssignable(paramType, type))
            return true;
    }
    return false;
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

void TypeChecker::errorDetailed(const SourceSpan& span,
                                const QString& message,
                                const QList<SourceSpan>& related,
                                const QString& explanation,
                                const QStringList& suggestions,
                                const QString& code)
{
    Diagnostic d;
    d.severity = Severity::Error;
    d.code = code;
    d.message = message;
    d.primary = span;
    d.related = related;
    d.explanation = explanation;
    d.suggestions = suggestions;
    m_diagnostics.push_back(d);
}

} // namespace abel
