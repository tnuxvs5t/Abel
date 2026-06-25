#pragma once

#include "abelcore/type.h"

#include <QChar>
#include <QHash>
#include <QList>
#include <QString>

#include <memory>
#include <variant>
#include <vector>

namespace abel {

struct AbelLocation;
struct AbelVectorValue;
struct AbelAnyValue;
struct AbelStructValue;
struct AbelFunctionValue;
struct FunctionDeclNode;
struct LambdaExprNode;

class AbelValue {
public:
    using VectorPtr = std::shared_ptr<AbelVectorValue>;
    using AnyPtr = std::shared_ptr<AbelAnyValue>;
    using StructPtr = std::shared_ptr<AbelStructValue>;
    using FunctionPtr = std::shared_ptr<AbelFunctionValue>;
    using Payload = std::variant<std::monostate, bool, qint64, double, QString, QChar, AbelLocation*, VectorPtr, AnyPtr, StructPtr, FunctionPtr>;

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
    static AbelValue makeVector(const AbelType& elementType, std::vector<AbelValue> elements);
    static AbelValue makeAny(const AbelValue& value);
    static AbelValue makeStruct(const QString& typeName, const std::vector<QString>& fieldOrder, QHash<QString, AbelValue> fields);
    static AbelValue makeFunction(const AbelType& type, FunctionPtr function);
    static AbelValue makeUnknown();

    const AbelType& type() const { return m_type; }
    const Payload& payload() const { return m_payload; }

    bool asBool() const;
    qint64 asInt() const;
    double asDouble() const;
    QString asString() const;
    QChar asChar() const;
    AbelLocation* asPointer() const;
    VectorPtr asVector() const;
    AnyPtr asAny() const;
    StructPtr asStruct() const;
    FunctionPtr asFunction() const;

    QString debugString() const;

private:
    AbelType m_type = makeType(TypeKind::Unknown);
    Payload m_payload;

    AbelValue(AbelType type, Payload payload);
};

struct AbelVectorValue {
    AbelType elementType;
    std::vector<AbelValue> elements;
};

struct AbelAnyValue {
    AbelValue value;
};

struct AbelStructValue {
    QString typeName;
    std::vector<QString> fieldOrder;
    QHash<QString, AbelValue> fields;
};

struct AbelFunctionValue {
    const LambdaExprNode* lambda = nullptr;
    const FunctionDeclNode* function = nullptr;
    QString packageName;
    QString moduleName;
    QString currentStruct;
    QList<QString> importedModules;
    QHash<QString, QString> importedModuleAliases;
    QHash<QString, AbelValue> valueCaptures;
    QHash<QString, AbelLocation*> refCaptures;
    QHash<QString, bool> refConstness;
};

AbelValue defaultValueForType(const AbelType& type);
AbelValue convertValue(const AbelValue& value, const AbelType& target);

} // namespace abel
