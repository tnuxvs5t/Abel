#include "abelcore/value.h"

#include <QStringList>

namespace abel {

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
    return AbelValue(makeType(kind), value);
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

QString AbelValue::debugString() const
{
    switch (m_type.kind) {
    case TypeKind::Void: return QStringLiteral("<void>");
    case TypeKind::Bool: return asBool() ? QStringLiteral("true") : QStringLiteral("false");
    case TypeKind::I32:
    case TypeKind::I64: return QString::number(asInt());
    case TypeKind::F64: return QString::number(asDouble(), 'g', 16);
    case TypeKind::Char: return QString(asChar());
    case TypeKind::Str: return asString();
    case TypeKind::Any: return QStringLiteral("<any>");
    case TypeKind::Unknown: return QStringLiteral("<unknown>");
    }
    return QStringLiteral("<unknown>");
}

AbelValue defaultValueForType(const AbelType& type)
{
    switch (type.kind) {
    case TypeKind::Void: return AbelValue::makeVoid();
    case TypeKind::Bool: return AbelValue::makeBool(false);
    case TypeKind::I32: return AbelValue::makeInt(0, TypeKind::I32);
    case TypeKind::I64: return AbelValue::makeInt(0, TypeKind::I64);
    case TypeKind::F64: return AbelValue::makeDouble(0.0);
    case TypeKind::Char: return AbelValue::makeChar(QChar());
    case TypeKind::Str: return AbelValue::makeString(QString());
    case TypeKind::Any:
    case TypeKind::Unknown: return AbelValue::makeUnknown();
    }
    return AbelValue::makeUnknown();
}

AbelValue convertValue(const AbelValue& value, const AbelType& target)
{
    if (value.type().kind == target.kind)
        return value;
    if (target.kind == TypeKind::I32 && value.type().isInteger())
        return AbelValue::makeInt(value.asInt(), TypeKind::I32);
    if (target.kind == TypeKind::I64 && value.type().isInteger())
        return AbelValue::makeInt(value.asInt(), TypeKind::I64);
    if (target.kind == TypeKind::F64 && value.type().isNumeric())
        return AbelValue::makeDouble(value.asDouble());
    if (target.kind == TypeKind::Any)
        return value;
    return AbelValue::makeUnknown();
}

} // namespace abel
