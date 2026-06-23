#include "abelcore/type.h"

#include "abelcore/ast.h"

namespace abel {

bool AbelType::operator==(const AbelType& other) const
{
    if (kind != other.kind || isConst != other.isConst)
        return false;
    if ((kind == TypeKind::Pointer || kind == TypeKind::Reference || kind == TypeKind::Vector) && pointee && other.pointee)
        return *pointee == *other.pointee;
    if (kind == TypeKind::Struct)
        return spelling == other.spelling;
    if (kind == TypeKind::Function) {
        if (!pointee || !other.pointee || *pointee != *other.pointee || params.size() != other.params.size())
            return false;
        for (size_t i = 0; i < params.size(); ++i) {
            if (params[i] != other.params[i])
                return false;
        }
        return true;
    }
    return true;
}

bool AbelType::isInteger() const
{
    return isSignedInteger() || isUnsignedInteger();
}

bool AbelType::isSignedInteger() const
{
    return kind == TypeKind::I8
        || kind == TypeKind::I16
        || kind == TypeKind::I32
        || kind == TypeKind::I64;
}

bool AbelType::isUnsignedInteger() const
{
    return kind == TypeKind::U8
        || kind == TypeKind::U16
        || kind == TypeKind::U32
        || kind == TypeKind::U64;
}

bool AbelType::isNumeric() const
{
    return isInteger() || kind == TypeKind::F64;
}

int AbelType::integerBitWidth() const
{
    switch (kind) {
    case TypeKind::I8:
    case TypeKind::U8:
        return 8;
    case TypeKind::I16:
    case TypeKind::U16:
        return 16;
    case TypeKind::I32:
    case TypeKind::U32:
        return 32;
    case TypeKind::I64:
    case TypeKind::U64:
        return 64;
    default:
        return 0;
    }
}

bool AbelType::isBool() const
{
    return kind == TypeKind::Bool;
}

bool AbelType::isVoid() const
{
    return kind == TypeKind::Void;
}

bool AbelType::isPointer() const
{
    return kind == TypeKind::Pointer;
}

bool AbelType::isReference() const
{
    return kind == TypeKind::Reference;
}

QString AbelType::displayName() const
{
    if (!spelling.isEmpty())
        return isConst ? QStringLiteral("const ") + spelling : spelling;
    QString out;
    switch (kind) {
    case TypeKind::Void: out = QStringLiteral("void"); break;
    case TypeKind::Bool: out = QStringLiteral("bool"); break;
    case TypeKind::I8: out = QStringLiteral("i8"); break;
    case TypeKind::I16: out = QStringLiteral("i16"); break;
    case TypeKind::I32: out = QStringLiteral("i32"); break;
    case TypeKind::I64: out = QStringLiteral("i64"); break;
    case TypeKind::U8: out = QStringLiteral("u8"); break;
    case TypeKind::U16: out = QStringLiteral("u16"); break;
    case TypeKind::U32: out = QStringLiteral("u32"); break;
    case TypeKind::U64: out = QStringLiteral("u64"); break;
    case TypeKind::F64: out = QStringLiteral("f64"); break;
    case TypeKind::Char: out = QStringLiteral("char"); break;
    case TypeKind::Str: out = QStringLiteral("str"); break;
    case TypeKind::Any: out = QStringLiteral("any"); break;
    case TypeKind::Pointer:
        out = pointee ? pointee->displayName() + QStringLiteral("*") : QStringLiteral("<unknown>*");
        break;
    case TypeKind::Reference:
        out = pointee ? pointee->displayName() + QStringLiteral("&") : QStringLiteral("<unknown>&");
        break;
    case TypeKind::Nullptr: out = QStringLiteral("nullptr"); break;
    case TypeKind::Vector:
        out = pointee ? QStringLiteral("vector<") + pointee->displayName() + QStringLiteral(">") : QStringLiteral("vector<?>");
        break;
    case TypeKind::Struct: out = spelling.isEmpty() ? QStringLiteral("<struct>") : spelling; break;
    case TypeKind::Function: {
        out = QStringLiteral("func ");
        out += pointee ? pointee->displayName() : QStringLiteral("<unknown>");
        out += QStringLiteral("(");
        for (size_t i = 0; i < params.size(); ++i) {
            if (i > 0)
                out += QStringLiteral(", ");
            out += params[i].displayName();
        }
        out += QStringLiteral(")");
        break;
    }
    case TypeKind::Unknown: out = QStringLiteral("<unknown>"); break;
    }
    return isConst ? QStringLiteral("const ") + out : out;
}

AbelType makeType(TypeKind kind, const QString& spelling)
{
    AbelType t;
    t.kind = kind;
    t.spelling = spelling;
    return t;
}

AbelType makeConstType(const AbelType& type)
{
    AbelType t = type;
    t.isConst = true;
    return t;
}

AbelType makePointerType(const AbelType& pointee)
{
    AbelType t;
    t.kind = TypeKind::Pointer;
    t.pointee = std::make_shared<AbelType>(pointee);
    return t;
}

AbelType makeReferenceType(const AbelType& referred)
{
    AbelType t;
    t.kind = TypeKind::Reference;
    t.pointee = std::make_shared<AbelType>(referred);
    return t;
}

AbelType makeVectorType(const AbelType& element)
{
    AbelType t;
    t.kind = TypeKind::Vector;
    t.pointee = std::make_shared<AbelType>(element);
    return t;
}

AbelType makeStructType(const QString& name)
{
    AbelType t;
    t.kind = TypeKind::Struct;
    t.spelling = name;
    return t;
}

AbelType makeFunctionType(const AbelType& returnType, std::vector<AbelType> params)
{
    AbelType t;
    t.kind = TypeKind::Function;
    t.pointee = std::make_shared<AbelType>(returnType);
    t.params = std::move(params);
    return t;
}

AbelType typeFromName(const QString& name)
{
    if (name == QStringLiteral("void"))
        return makeType(TypeKind::Void, name);
    if (name == QStringLiteral("bool"))
        return makeType(TypeKind::Bool, name);
    if (name == QStringLiteral("i8"))
        return makeType(TypeKind::I8, name);
    if (name == QStringLiteral("i16"))
        return makeType(TypeKind::I16, name);
    if (name == QStringLiteral("int") || name == QStringLiteral("i32"))
        return makeType(TypeKind::I32, name);
    if (name == QStringLiteral("long") || name == QStringLiteral("ll") || name == QStringLiteral("i64"))
        return makeType(TypeKind::I64, name);
    if (name == QStringLiteral("u8"))
        return makeType(TypeKind::U8, name);
    if (name == QStringLiteral("u16"))
        return makeType(TypeKind::U16, name);
    if (name == QStringLiteral("u32"))
        return makeType(TypeKind::U32, name);
    if (name == QStringLiteral("u64"))
        return makeType(TypeKind::U64, name);
    if (name == QStringLiteral("double") || name == QStringLiteral("f64"))
        return makeType(TypeKind::F64, name);
    if (name == QStringLiteral("char"))
        return makeType(TypeKind::Char, name);
    if (name == QStringLiteral("str"))
        return makeType(TypeKind::Str, name);
    if (name == QStringLiteral("any"))
        return makeType(TypeKind::Any, name);
    return makeStructType(name);
}

AbelType typeFromAst(const TypeNode& node)
{
    AbelType base = typeFromName(node.name);
    if (node.name == QStringLiteral("vector") && node.elementType) {
        base = makeVectorType(typeFromAst(*node.elementType));
    } else if (node.name == QStringLiteral("func") && node.elementType) {
        std::vector<AbelType> params;
        params.reserve(node.functionParamTypes.size());
        for (const auto& param : node.functionParamTypes)
            params.push_back(typeFromAst(*param));
        base = makeFunctionType(typeFromAst(*node.elementType), std::move(params));
    }
    if (node.isConst)
        base = makeConstType(base);
    for (int i = 0; i < node.pointerDepth; ++i)
        base = makePointerType(base);
    if (node.isReference)
        base = makeReferenceType(base);
    return base;
}

bool canAssignValue(const AbelType& target, const AbelType& source)
{
    AbelType targetValue = target;
    AbelType sourceValue = source;
    targetValue.isConst = false;
    sourceValue.isConst = false;
    if (targetValue.kind == TypeKind::Unknown || sourceValue.kind == TypeKind::Unknown)
        return true;
    if (targetValue.isReference()) {
        if (!targetValue.pointee)
            return false;
        AbelType referred = *targetValue.pointee;
        referred.isConst = false;
        return canAssignValue(referred, sourceValue);
    }
    if (targetValue.isPointer() && sourceValue.kind == TypeKind::Nullptr)
        return true;
    if (targetValue.kind == TypeKind::Any)
        return true;
    if (targetValue.kind == sourceValue.kind)
        return (targetValue.kind != TypeKind::Pointer && targetValue.kind != TypeKind::Vector && targetValue.kind != TypeKind::Function)
            || !targetValue.pointee
            || !sourceValue.pointee
            || targetValue == sourceValue;
    if (targetValue.isInteger() && sourceValue.isNumeric())
        return true;
    if (targetValue.kind == TypeKind::F64 && sourceValue.isNumeric())
        return true;
    return false;
}

} // namespace abel
