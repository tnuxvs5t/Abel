#pragma once

#include <QString>

#include <memory>

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
    Pointer,
    Reference,
    Nullptr,
    Vector,
    Struct,
    Unknown,
};

struct AbelType {
    TypeKind kind = TypeKind::Unknown;
    QString spelling;
    std::shared_ptr<AbelType> pointee;

    bool operator==(const AbelType& other) const;
    bool operator!=(const AbelType& other) const { return !(*this == other); }
    bool isInteger() const;
    bool isNumeric() const;
    bool isBool() const;
    bool isVoid() const;
    bool isPointer() const;
    bool isReference() const;
    QString displayName() const;
};

AbelType makeType(TypeKind kind, const QString& spelling = QString());
AbelType makePointerType(const AbelType& pointee);
AbelType makeReferenceType(const AbelType& referred);
AbelType makeVectorType(const AbelType& element);
AbelType makeStructType(const QString& name);
AbelType typeFromName(const QString& name);
AbelType typeFromAst(const TypeNode& node);
bool canAssignValue(const AbelType& target, const AbelType& source);

} // namespace abel
