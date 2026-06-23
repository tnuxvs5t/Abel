#include "abelcore/value.h"

#include <QStringList>

namespace abel {

namespace {

qint64 normalizeIntegerValue(qint64 value, TypeKind kind)
{
    switch (kind) {
    case TypeKind::I8:
        return static_cast<qint8>(value);
    case TypeKind::I16:
        return static_cast<qint16>(value);
    case TypeKind::I32:
        return static_cast<qint32>(value);
    case TypeKind::I64:
        return value;
    case TypeKind::U8:
        return static_cast<quint8>(value);
    case TypeKind::U16:
        return static_cast<quint16>(value);
    case TypeKind::U32:
        return static_cast<qint64>(static_cast<quint32>(value));
    case TypeKind::U64:
        return static_cast<qint64>(static_cast<quint64>(value));
    default:
        return value;
    }
}

} // namespace

AbelValue::AbelValue(AbelType type, Payload payload)
    : m_type(std::move(type))
    , m_payload(std::move(payload))
{
}

AbelValue AbelValue::makeVoid()
{
    return AbelValue(makeType(TypeKind::Void), std::monostate{});
}

AbelValue AbelValue::makeBool(bool value)
{
    return AbelValue(makeType(TypeKind::Bool), value);
}

AbelValue AbelValue::makeInt(qint64 value, TypeKind kind)
{
    return AbelValue(makeType(kind), normalizeIntegerValue(value, kind));
}

AbelValue AbelValue::makeDouble(double value)
{
    return AbelValue(makeType(TypeKind::F64), value);
}

AbelValue AbelValue::makeString(const QString& value)
{
    return AbelValue(makeType(TypeKind::Str), value);
}

AbelValue AbelValue::makeChar(QChar value)
{
    return AbelValue(makeType(TypeKind::Char), value);
}

AbelValue AbelValue::makePointer(const AbelType& pointee, AbelLocation* location)
{
    return AbelValue(makePointerType(pointee), location);
}

AbelValue AbelValue::makeNullPointer(const AbelType& pointee)
{
    return AbelValue(makePointerType(pointee), static_cast<AbelLocation*>(nullptr));
}

AbelValue AbelValue::makeNullptr()
{
    return AbelValue(makeType(TypeKind::Nullptr), static_cast<AbelLocation*>(nullptr));
}

AbelValue AbelValue::makeVector(const AbelType& elementType, std::vector<AbelValue> elements)
{
    auto vector = std::make_shared<AbelVectorValue>();
    vector->elementType = elementType;
    vector->elements = std::move(elements);
    return AbelValue(makeVectorType(elementType), vector);
}

AbelValue AbelValue::makeAny(const AbelValue& value)
{
    auto any = std::make_shared<AbelAnyValue>();
    any->value = value;
    return AbelValue(makeType(TypeKind::Any), any);
}

AbelValue AbelValue::makeStruct(const QString& typeName, const std::vector<QString>& fieldOrder, QHash<QString, AbelValue> fields)
{
    auto object = std::make_shared<AbelStructValue>();
    object->typeName = typeName;
    object->fieldOrder = fieldOrder;
    object->fields = std::move(fields);
    return AbelValue(makeStructType(typeName), object);
}

AbelValue AbelValue::makeFunction(const AbelType& type, FunctionPtr function)
{
    return AbelValue(type, std::move(function));
}

AbelValue AbelValue::makeUnknown()
{
    return AbelValue(makeType(TypeKind::Unknown), std::monostate{});
}

bool AbelValue::asBool() const
{
    return std::get<bool>(m_payload);
}

qint64 AbelValue::asInt() const
{
    return std::get<qint64>(m_payload);
}

double AbelValue::asDouble() const
{
    if (m_type.isInteger())
        return static_cast<double>(asInt());
    return std::get<double>(m_payload);
}

QString AbelValue::asString() const
{
    return std::get<QString>(m_payload);
}

QChar AbelValue::asChar() const
{
    return std::get<QChar>(m_payload);
}

AbelLocation* AbelValue::asPointer() const
{
    return std::get<AbelLocation*>(m_payload);
}

AbelValue::VectorPtr AbelValue::asVector() const
{
    return std::get<VectorPtr>(m_payload);
}

AbelValue::AnyPtr AbelValue::asAny() const
{
    return std::get<AnyPtr>(m_payload);
}

AbelValue::StructPtr AbelValue::asStruct() const
{
    return std::get<StructPtr>(m_payload);
}

AbelValue::FunctionPtr AbelValue::asFunction() const
{
    return std::get<FunctionPtr>(m_payload);
}

QString AbelValue::debugString() const
{
    switch (m_type.kind) {
    case TypeKind::Void: return QStringLiteral("<void>");
    case TypeKind::Bool: return asBool() ? QStringLiteral("true") : QStringLiteral("false");
    case TypeKind::I8:
    case TypeKind::I16:
    case TypeKind::I32:
    case TypeKind::I64: return QString::number(asInt());
    case TypeKind::U8:
    case TypeKind::U16:
    case TypeKind::U32:
    case TypeKind::U64: return QString::number(static_cast<quint64>(asInt()));
    case TypeKind::F64: return QString::number(asDouble(), 'g', 16);
    case TypeKind::Char: return QString(asChar());
    case TypeKind::Str: return asString();
    case TypeKind::Any: return asAny()->value.debugString();
    case TypeKind::Pointer:
        return asPointer() ? QStringLiteral("<ptr>") : QStringLiteral("nullptr");
    case TypeKind::Reference: return QStringLiteral("<ref>");
    case TypeKind::Nullptr: return QStringLiteral("nullptr");
    case TypeKind::Vector:
        return QStringLiteral("<vector len=%1>").arg(asVector()->elements.size());
    case TypeKind::Struct:
        return QStringLiteral("<struct %1>").arg(asStruct()->typeName);
    case TypeKind::Function:
        return QStringLiteral("<function>");
    case TypeKind::Unknown: return QStringLiteral("<unknown>");
    }
    return QStringLiteral("<unknown>");
}

AbelValue defaultValueForType(const AbelType& type)
{
    switch (type.kind) {
    case TypeKind::Void: return AbelValue::makeVoid();
    case TypeKind::Bool: return AbelValue::makeBool(false);
    case TypeKind::I8: return AbelValue::makeInt(0, TypeKind::I8);
    case TypeKind::I16: return AbelValue::makeInt(0, TypeKind::I16);
    case TypeKind::I32: return AbelValue::makeInt(0, TypeKind::I32);
    case TypeKind::I64: return AbelValue::makeInt(0, TypeKind::I64);
    case TypeKind::U8: return AbelValue::makeInt(0, TypeKind::U8);
    case TypeKind::U16: return AbelValue::makeInt(0, TypeKind::U16);
    case TypeKind::U32: return AbelValue::makeInt(0, TypeKind::U32);
    case TypeKind::U64: return AbelValue::makeInt(0, TypeKind::U64);
    case TypeKind::F64: return AbelValue::makeDouble(0.0);
    case TypeKind::Char: return AbelValue::makeChar(QChar());
    case TypeKind::Str: return AbelValue::makeString(QString());
    case TypeKind::Pointer:
        return type.pointee ? AbelValue::makeNullPointer(*type.pointee) : AbelValue::makeUnknown();
    case TypeKind::Reference:
        return AbelValue::makeUnknown();
    case TypeKind::Nullptr:
        return AbelValue::makeNullptr();
    case TypeKind::Vector:
        return type.pointee ? AbelValue::makeVector(*type.pointee, {}) : AbelValue::makeUnknown();
    case TypeKind::Any:
        return AbelValue::makeAny(AbelValue::makeVoid());
    case TypeKind::Struct:
        return AbelValue::makeUnknown();
    case TypeKind::Function:
        return AbelValue::makeUnknown();
    case TypeKind::Unknown: return AbelValue::makeUnknown();
    }
    return AbelValue::makeUnknown();
}

AbelValue convertValue(const AbelValue& value, const AbelType& target)
{
    if (target.kind == TypeKind::Vector && value.type().kind == TypeKind::Vector && target.pointee) {
        auto copied = value.asVector();
        std::vector<AbelValue> elements;
        elements.reserve(copied->elements.size());
        for (const auto& element : copied->elements)
            elements.push_back(convertValue(element, element.type()));
        return AbelValue::makeVector(*target.pointee, std::move(elements));
    }
    if (target.kind == TypeKind::Struct && value.type().kind == TypeKind::Struct && target.spelling == value.type().spelling) {
        auto copied = value.asStruct();
        QHash<QString, AbelValue> fields;
        for (const auto& name : copied->fieldOrder)
            fields.insert(name, convertValue(copied->fields.value(name), copied->fields.value(name).type()));
        return AbelValue::makeStruct(copied->typeName, copied->fieldOrder, std::move(fields));
    }
    if (target.kind == TypeKind::Function && value.type().kind == TypeKind::Function && target == value.type())
        return value;
    if (value.type().kind == target.kind)
        return value;
    if (target.isInteger() && value.type().isNumeric()) {
        const qint64 converted = value.type().kind == TypeKind::F64
            ? static_cast<qint64>(value.asDouble())
            : value.asInt();
        return AbelValue::makeInt(converted, target.kind);
    }
    if (target.kind == TypeKind::F64 && value.type().isNumeric())
        return AbelValue::makeDouble(value.asDouble());
    if (target.kind == TypeKind::Pointer && value.type().kind == TypeKind::Nullptr && target.pointee)
        return AbelValue::makeNullPointer(*target.pointee);
    if (target.kind == TypeKind::Pointer && value.type().kind == TypeKind::Pointer)
        return value;
    if (target.kind == TypeKind::Any)
        return AbelValue::makeAny(value);
    return AbelValue::makeUnknown();
}

} // namespace abel
