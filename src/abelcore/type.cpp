#include "abelcore/type.h"

#include "abelcore/ast.h"

namespace abel {

bool AbelType::isInteger() const
{
    return kind == TypeKind::I32 || kind == TypeKind::I64;
}

bool AbelType::isNumeric() const
{
    return isInteger() || kind == TypeKind::F64;
}

bool AbelType::isBool() const
{
    return kind == TypeKind::Bool;
}

bool AbelType::isVoid() const
{
    return kind == TypeKind::Void;
}

QString AbelType::displayName() const
{
    if (!spelling.isEmpty())
        return spelling;
    switch (kind) {
    case TypeKind::Void: return QStringLiteral("void");
    case TypeKind::Bool: return QStringLiteral("bool");
    case TypeKind::I32: return QStringLiteral("i32");
    case TypeKind::I64: return QStringLiteral("i64");
    case TypeKind::F64: return QStringLiteral("f64");
    case TypeKind::Char: return QStringLiteral("char");
    case TypeKind::Str: return QStringLiteral("str");
    case TypeKind::Any: return QStringLiteral("any");
    case TypeKind::Unknown: return QStringLiteral("<unknown>");
    }
    return QStringLiteral("<unknown>");
}

AbelType makeType(TypeKind kind, const QString& spelling)
{
    AbelType t;
    t.kind = kind;
    t.spelling = spelling;
    return t;
}

AbelType typeFromName(const QString& name)
{
    if (name == QStringLiteral("void"))
        return makeType(TypeKind::Void, name);
    if (name == QStringLiteral("bool"))
        return makeType(TypeKind::Bool, name);
    if (name == QStringLiteral("int") || name == QStringLiteral("i32"))
        return makeType(TypeKind::I32, name);
    if (name == QStringLiteral("long") || name == QStringLiteral("ll") || name == QStringLiteral("i64"))
        return makeType(TypeKind::I64, name);
    if (name == QStringLiteral("double") || name == QStringLiteral("f64"))
        return makeType(TypeKind::F64, name);
    if (name == QStringLiteral("char"))
        return makeType(TypeKind::Char, name);
    if (name == QStringLiteral("str"))
        return makeType(TypeKind::Str, name);
    if (name == QStringLiteral("any"))
        return makeType(TypeKind::Any, name);
    return makeType(TypeKind::Unknown, name);
}

AbelType typeFromAst(const TypeNode& node)
{
    if (node.elementType || node.pointerDepth > 0 || node.isReference)
        return makeType(TypeKind::Unknown, node.displayName());
    return typeFromName(node.name);
}

bool canAssignValue(const AbelType& target, const AbelType& source)
{
    if (target.kind == TypeKind::Any)
        return true;
    if (target.kind == source.kind)
        return true;
    if (target.isInteger() && source.isInteger())
        return true;
    if (target.kind == TypeKind::F64 && source.isNumeric())
        return true;
    return false;
}

} // namespace abel
