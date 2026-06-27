#pragma once

#include "abelcore/type.h"

#include <QChar>
#include <QHash>
#include <QList>
#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace abel {

struct SourceSpan;
struct AbelLocation;
struct AbelVectorValue;
struct AbelAnyValue;
struct AbelStructValue;
struct AbelFunctionValue;
struct AbelDynamicObject;
class AbelRuntimeContext;
struct FunctionDeclNode;
struct LambdaExprNode;

class AbelValue {
public:
    using VectorPtr = std::shared_ptr<AbelVectorValue>;
    using AnyPtr = std::shared_ptr<AbelAnyValue>;
    using StructPtr = std::shared_ptr<AbelStructValue>;
    using FunctionPtr = std::shared_ptr<AbelFunctionValue>;
    using DynamicObjectPtr = std::shared_ptr<AbelDynamicObject>;
    using Payload = std::variant<std::monostate, bool, qint64, double, QString, QChar, AbelLocation*, VectorPtr, AnyPtr, StructPtr, FunctionPtr, DynamicObjectPtr>;

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
    static AbelValue makeDynamicObject(DynamicObjectPtr object);
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
    DynamicObjectPtr asDynamicObject() const;

    bool isBoxedAny() const;
    bool isDynamicObject() const;

    QString debugString() const;

private:
    AbelType m_type = makeType(TypeKind::Unknown);
    Payload m_payload;

    AbelValue(AbelType type, Payload payload);
};

bool abelValueEquals(const AbelValue& lhs, const AbelValue& rhs);

class AbelValueKey {
public:
    AbelValueKey() = default;

    static std::optional<AbelValueKey> fromValue(const AbelValue& value, QString* error = nullptr);

    const AbelValue& value() const { return m_value; }
    quint64 stableHash() const { return m_hash; }

    bool operator==(const AbelValueKey& other) const;
    bool operator!=(const AbelValueKey& other) const { return !(*this == other); }

private:
    AbelValue m_value = AbelValue::makeUnknown();
    quint64 m_hash = 0;

    AbelValueKey(AbelValue value, quint64 hash);
};

size_t qHash(const AbelValueKey& key, size_t seed = 0);

struct AbelVectorValue {
    AbelType elementType;
    std::vector<AbelValue> elements;
};

struct AbelAnyValue {
    AbelValue value;
};

struct AbelDynamicObject {
    using Get = std::function<AbelValue(const AbelValue& key, AbelRuntimeContext& ctx, const SourceSpan& span)>;
    using Set = std::function<void(const AbelValue& key, const AbelValue& value, AbelRuntimeContext& ctx, const SourceSpan& span)>;
    using Equals = std::function<std::optional<bool>(const AbelValue& other, AbelRuntimeContext& ctx, const SourceSpan& span)>;
    using Debug = std::function<QString()>;

    QString kind = QStringLiteral("dynamic");
    Get get;
    Set set;
    Equals equals;
    Debug debug;
};

struct AbelStructValue {
    QString typeName;
    std::vector<QString> fieldOrder;
    QHash<QString, AbelValue> fields;
};

struct AbelFunctionValue {
    using Invoke = std::function<AbelValue(const std::vector<AbelValue>&, AbelRuntimeContext&, const SourceSpan&)>;

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
    Invoke invoke;
};

AbelValue defaultValueForType(const AbelType& type);
AbelValue convertValue(const AbelValue& value, const AbelType& target);
AbelValue unboxAny(const AbelValue& value);

} // namespace abel
