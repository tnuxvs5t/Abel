#pragma once

#include "abelcore/runtime.h"

#include <functional>
#include <optional>
#include <vector>

namespace abel {

struct BuiltinFunctionCall {
    AbelRuntimeContext& ctx;
    QString name;
    std::vector<AbelValue> args;
    std::vector<SourceSpan> argSpans;
    SourceSpan callSpan;
    std::function<std::optional<QString>(const AbelValue&, const SourceSpan&)> stringify;
};

struct BuiltinMethodCall {
    AbelRuntimeContext& ctx;
    AbelLocation* receiverLocation = nullptr;
    AbelValue receiver;
    QString name;
    std::vector<AbelValue> args;
    std::vector<SourceSpan> argSpans;
    SourceSpan callSpan;
    std::function<AbelValue(const AbelType&, const SourceSpan&)> defaultValue;
};

struct BuiltinFunctionDesc {
    QString name;
    int minArgs = 0;
    int maxArgs = 0; // -1 means unlimited
    bool variadic = false;
    std::function<AbelValue(BuiltinFunctionCall&)> runtime;
    QString doc;
};

struct BuiltinMethodDesc {
    TypeKind receiverKind = TypeKind::Unknown;
    QString name;
    int minArgs = 0;
    int maxArgs = 0; // -1 means unlimited
    bool mutatesReceiver = false;
    std::function<AbelValue(BuiltinMethodCall&)> runtime;
    QString doc;
};

class BuiltinRegistry {
public:
    static BuiltinRegistry makeDefault();

    void registerFunction(BuiltinFunctionDesc desc);
    void registerMethod(BuiltinMethodDesc desc);

    bool hasFunction(const QString& name) const;
    bool hasMethod(const AbelType& receiverType, const QString& name) const;

    AbelValue callFunction(BuiltinFunctionCall call) const;
    AbelValue callMethod(BuiltinMethodCall call) const;

private:
    std::vector<BuiltinFunctionDesc> m_functions;
    std::vector<BuiltinMethodDesc> m_methods;

    const BuiltinFunctionDesc* findFunction(const QString& name) const;
    const BuiltinMethodDesc* findMethod(const AbelType& receiverType, const QString& name) const;
};

} // namespace abel
