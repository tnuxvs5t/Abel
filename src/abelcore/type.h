#pragma once

#include <QString>

namespace abel {

struct TypeNode;

enum class TypeKind {
    Void,
    Bool,
    I32,
    I64,
    F64,
    Char,
    Str,
    Any,
    Unknown,
};

struct AbelType {
    TypeKind kind = TypeKind::Unknown;
    QString spelling;

    bool operator==(const AbelType& other) const { return kind == other.kind; }
    bool operator!=(const AbelType& other) const { return !(*this == other); }
    bool isInteger() const;
    bool isNumeric() const;
    bool isBool() const;
    bool isVoid() const;
    QString displayName() const;
};

AbelType makeType(TypeKind kind, const QString& spelling = QString());
AbelType typeFromName(const QString& name);
AbelType typeFromAst(const TypeNode& node);
bool canAssignValue(const AbelType& target, const AbelType& source);

} // namespace abel
