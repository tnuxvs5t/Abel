#pragma once

#include "abelcore/type.h"

#include <QChar>
#include <QString>

#include <variant>

namespace abel {

struct AbelLocation;

class AbelValue {
public:
    using Payload = std::variant<std::monostate, bool, qint64, double, QString, QChar, AbelLocation*>;

    AbelValue() = default;

    static AbelValue makeVoid();
    static AbelValue makeBool(bool value);
    static AbelValue makeInt(qint64 value, TypeKind kind = TypeKind::I32);
    static AbelValue makeDouble(double value);
    static AbelValue makeString(const QString& value);
    static AbelValue makeChar(QChar value);
    static AbelValue makePointer(const AbelType& pointee, AbelLocation* location);
    static AbelValue makeNullPointer(const AbelType& pointee);
    static AbelValue makeNullptr();
    static AbelValue makeUnknown();

    const AbelType& type() const { return m_type; }
    const Payload& payload() const { return m_payload; }

    bool asBool() const;
    qint64 asInt() const;
    double asDouble() const;
    QString asString() const;
    QChar asChar() const;
    AbelLocation* asPointer() const;

    QString debugString() const;

private:
    AbelType m_type = makeType(TypeKind::Unknown);
    Payload m_payload;

    AbelValue(AbelType type, Payload payload);
};

AbelValue defaultValueForType(const AbelType& type);
AbelValue convertValue(const AbelValue& value, const AbelType& target);

} // namespace abel
