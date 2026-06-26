#include "abelcore/value.h"

#include <QStringList>

#include <cstring>

namespace abel {

namespace {

constexpr quint64 kFnvOffset = 14695981039346656037ull;
constexpr quint64 kFnvPrime = 1099511628211ull;

quint64 mixByte(quint64 hash, unsigned char byte)
{
    hash ^= byte;
    hash *= kFnvPrime;
    return hash;
}

quint64 mixUInt64(quint64 hash, quint64 value)
{
    for (int i = 0; i < 8; ++i)
        hash = mixByte(hash, static_cast<unsigned char>((value >> (i * 8)) & 0xff));
    return hash;
}

quint64 mixInt64(quint64 hash, qint64 value)
{
    return mixUInt64(hash, static_cast<quint64>(value));
}

quint64 doubleBits(double value)
{
    if (value == 0.0)
        value = 0.0;
    quint64 bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

quint64 mixString(quint64 hash, const QString& value)
{
    hash = mixUInt64(hash, static_cast<quint64>(value.size()));
    for (QChar ch : value)
        hash = mixUInt64(hash, static_cast<quint64>(ch.unicode()));
    return hash;
}

quint64 mixType(quint64 hash, const AbelType& type)
{
    hash = mixUInt64(hash, static_cast<quint64>(type.kind));
    hash = mixByte(hash, type.isConst ? 1 : 0);
    hash = mixString(hash, type.spelling);
    if (type.pointee)
        hash = mixType(hash, *type.pointee);
    hash = mixUInt64(hash, static_cast<quint64>(type.params.size()));
    for (const auto& param : type.params)
        hash = mixType(hash, param);
    return hash;
}

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

std::optional<AbelValue> keyValueCopy(const AbelValue& value, QString* error)
{
    switch (value.type().kind) {
    case TypeKind::Any:
        return keyValueCopy(value.asAny()->value, error);
    case TypeKind::Bool:
        return AbelValue::makeBool(value.asBool());
    case TypeKind::I8:
    case TypeKind::I16:
    case TypeKind::I32:
    case TypeKind::I64:
    case TypeKind::U8:
    case TypeKind::U16:
    case TypeKind::U32:
    case TypeKind::U64:
        return AbelValue::makeInt(value.asInt(), value.type().kind);
    case TypeKind::F64:
        return AbelValue::makeDouble(value.asDouble());
    case TypeKind::Char:
        return AbelValue::makeChar(value.asChar());
    case TypeKind::Str:
        return AbelValue::makeString(value.asString());
    case TypeKind::Nullptr:
        return AbelValue::makeNullptr();
    case TypeKind::Vector: {
        auto vector = value.asVector();
        std::vector<AbelValue> elements;
        elements.reserve(vector->elements.size());
        for (const auto& element : vector->elements) {
            auto copied = keyValueCopy(element, error);
            if (!copied)
                return std::nullopt;
            elements.push_back(std::move(*copied));
        }
        return AbelValue::makeVector(vector->elementType, std::move(elements));
    }
    case TypeKind::Void:
    case TypeKind::Pointer:
    case TypeKind::Reference:
    case TypeKind::Struct:
    case TypeKind::Function:
    case TypeKind::Unknown:
        if (error) {
            *error = QStringLiteral("unsupported AbelValue key type '%1'")
                         .arg(value.type().displayName());
        }
        return std::nullopt;
    }
    if (error)
        *error = QStringLiteral("unsupported AbelValue key type '%1'")
                     .arg(value.type().displayName());
    return std::nullopt;
}

quint64 valueHash(const AbelValue& value)
{
    quint64 hash = mixType(kFnvOffset, value.type());
    switch (value.type().kind) {
    case TypeKind::Void:
    case TypeKind::Reference:
    case TypeKind::Pointer:
    case TypeKind::Struct:
    case TypeKind::Function:
    case TypeKind::Unknown:
        return hash;
    case TypeKind::Bool:
        return mixByte(hash, value.asBool() ? 1 : 0);
    case TypeKind::I8:
    case TypeKind::I16:
    case TypeKind::I32:
    case TypeKind::I64:
    case TypeKind::U8:
    case TypeKind::U16:
    case TypeKind::U32:
    case TypeKind::U64:
        return mixInt64(hash, value.asInt());
    case TypeKind::F64:
        return mixUInt64(hash, doubleBits(value.asDouble()));
    case TypeKind::Char:
        return mixUInt64(hash, static_cast<quint64>(value.asChar().unicode()));
    case TypeKind::Str:
        return mixString(hash, value.asString());
    case TypeKind::Any:
        return valueHash(value.asAny()->value);
    case TypeKind::Nullptr:
        return hash;
    case TypeKind::Vector: {
        auto vector = value.asVector();
        hash = mixUInt64(hash, static_cast<quint64>(vector->elements.size()));
        for (const auto& element : vector->elements)
            hash = mixUInt64(hash, valueHash(element));
        return hash;
    }
    }
    return hash;
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

bool abelValueEquals(const AbelValue& lhs, const AbelValue& rhs)
{
    if (lhs.type().kind == TypeKind::Any)
        return abelValueEquals(lhs.asAny()->value, rhs);
    if (rhs.type().kind == TypeKind::Any)
        return abelValueEquals(lhs, rhs.asAny()->value);
    if (lhs.type() != rhs.type())
        return false;
    switch (lhs.type().kind) {
    case TypeKind::Void:
    case TypeKind::Nullptr:
        return true;
    case TypeKind::Bool:
        return lhs.asBool() == rhs.asBool();
    case TypeKind::I8:
    case TypeKind::I16:
    case TypeKind::I32:
    case TypeKind::I64:
    case TypeKind::U8:
    case TypeKind::U16:
    case TypeKind::U32:
    case TypeKind::U64:
        return lhs.asInt() == rhs.asInt();
    case TypeKind::F64:
        return doubleBits(lhs.asDouble()) == doubleBits(rhs.asDouble());
    case TypeKind::Char:
        return lhs.asChar() == rhs.asChar();
    case TypeKind::Str:
        return lhs.asString() == rhs.asString();
    case TypeKind::Pointer:
        return lhs.asPointer() == rhs.asPointer();
    case TypeKind::Reference:
        return false;
    case TypeKind::Any:
        return false;
    case TypeKind::Vector: {
        auto lhsVector = lhs.asVector();
        auto rhsVector = rhs.asVector();
        if (lhsVector->elements.size() != rhsVector->elements.size())
            return false;
        for (size_t i = 0; i < lhsVector->elements.size(); ++i) {
            if (!abelValueEquals(lhsVector->elements[i], rhsVector->elements[i]))
                return false;
        }
        return true;
    }
    case TypeKind::Struct: {
        auto lhsStruct = lhs.asStruct();
        auto rhsStruct = rhs.asStruct();
        if (lhsStruct->typeName != rhsStruct->typeName
            || lhsStruct->fieldOrder != rhsStruct->fieldOrder) {
            return false;
        }
        for (const auto& name : lhsStruct->fieldOrder) {
            if (!rhsStruct->fields.contains(name)
                || !abelValueEquals(lhsStruct->fields.value(name), rhsStruct->fields.value(name))) {
                return false;
            }
        }
        return true;
    }
    case TypeKind::Function:
        return lhs.asFunction() == rhs.asFunction();
    case TypeKind::Unknown:
        return false;
    }
    return false;
}

AbelValueKey::AbelValueKey(AbelValue value, quint64 hash)
    : m_value(std::move(value))
    , m_hash(hash)
{
}

std::optional<AbelValueKey> AbelValueKey::fromValue(const AbelValue& value, QString* error)
{
    auto copied = keyValueCopy(value, error);
    if (!copied)
        return std::nullopt;
    const quint64 hash = valueHash(*copied);
    return AbelValueKey(std::move(*copied), hash);
}

bool AbelValueKey::operator==(const AbelValueKey& other) const
{
    return m_hash == other.m_hash && abelValueEquals(m_value, other.m_value);
}

size_t qHash(const AbelValueKey& key, size_t seed)
{
    return static_cast<size_t>(key.stableHash() ^ static_cast<quint64>(seed));
}

} // namespace abel
