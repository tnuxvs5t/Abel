#include "abelcore/builtin_registry.h"

namespace abel {

namespace {

bool arityMatches(int argc, int minArgs, int maxArgs)
{
    if (argc < minArgs)
        return false;
    if (maxArgs >= 0 && argc > maxArgs)
        return false;
    return true;
}

AbelValue convertBuiltinArg(BuiltinMethodCall& call, int index, const AbelType& target)
{
    const AbelValue& value = call.args[static_cast<size_t>(index)];
    if (!canAssignValue(target, value.type())) {
        const SourceSpan span = index < static_cast<int>(call.argSpans.size()) ? call.argSpans[static_cast<size_t>(index)] : call.callSpan;
        call.ctx.error(QStringLiteral("E0403"),
                       QStringLiteral("cannot pass %1 to builtin argument of type %2")
                           .arg(value.type().displayName(), target.displayName()),
                       span);
        return AbelValue::makeUnknown();
    }
    return convertValue(value, target);
}

AbelValue vectorLen(BuiltinMethodCall& call)
{
    return AbelValue::makeInt(static_cast<qint64>(call.receiver.asVector()->elements.size()), TypeKind::I32);
}

AbelValue vectorEmpty(BuiltinMethodCall& call)
{
    return AbelValue::makeBool(call.receiver.asVector()->elements.empty());
}

AbelValue vectorPush(BuiltinMethodCall& call)
{
    const AbelType& elementType = *call.receiver.type().pointee;
    AbelValue value = convertBuiltinArg(call, 0, elementType);
    if (call.ctx.hasError())
        return AbelValue::makeUnknown();
    call.receiver.asVector()->elements.push_back(value);
    return AbelValue::makeVoid();
}

AbelValue vectorPop(BuiltinMethodCall& call)
{
    auto vector = call.receiver.asVector();
    if (vector->elements.empty()) {
        call.ctx.error(QStringLiteral("E0404"), QStringLiteral("cannot pop empty vector"), call.callSpan);
        return AbelValue::makeUnknown();
    }
    AbelValue value = vector->elements.back();
    vector->elements.pop_back();
    return value;
}

AbelValue vectorFront(BuiltinMethodCall& call)
{
    auto vector = call.receiver.asVector();
    if (vector->elements.empty()) {
        call.ctx.error(QStringLiteral("E0405"), QStringLiteral("cannot read front of empty vector"), call.callSpan);
        return AbelValue::makeUnknown();
    }
    return vector->elements.front();
}

AbelValue vectorBack(BuiltinMethodCall& call)
{
    auto vector = call.receiver.asVector();
    if (vector->elements.empty()) {
        call.ctx.error(QStringLiteral("E0406"), QStringLiteral("cannot read back of empty vector"), call.callSpan);
        return AbelValue::makeUnknown();
    }
    return vector->elements.back();
}

} // namespace

BuiltinRegistry BuiltinRegistry::makeDefault()
{
    BuiltinRegistry registry;

    registry.registerMethod({TypeKind::Vector, QStringLiteral("len"), 0, 0, false, vectorLen, QStringLiteral("vector length")});
    registry.registerMethod({TypeKind::Vector, QStringLiteral("empty"), 0, 0, false, vectorEmpty, QStringLiteral("vector empty test")});
    registry.registerMethod({TypeKind::Vector, QStringLiteral("push"), 1, 1, true, vectorPush, QStringLiteral("vector push")});
    registry.registerMethod({TypeKind::Vector, QStringLiteral("pop"), 0, 0, true, vectorPop, QStringLiteral("vector pop")});
    registry.registerMethod({TypeKind::Vector, QStringLiteral("front"), 0, 0, false, vectorFront, QStringLiteral("vector front")});
    registry.registerMethod({TypeKind::Vector, QStringLiteral("back"), 0, 0, false, vectorBack, QStringLiteral("vector back")});

    return registry;
}

void BuiltinRegistry::registerFunction(BuiltinFunctionDesc desc)
{
    m_functions.push_back(std::move(desc));
}

void BuiltinRegistry::registerMethod(BuiltinMethodDesc desc)
{
    m_methods.push_back(std::move(desc));
}

bool BuiltinRegistry::hasFunction(const QString& name) const
{
    return findFunction(name) != nullptr;
}

bool BuiltinRegistry::hasMethod(const AbelType& receiverType, const QString& name) const
{
    return findMethod(receiverType, name) != nullptr;
}

AbelValue BuiltinRegistry::callFunction(BuiltinFunctionCall call) const
{
    const BuiltinFunctionDesc* desc = findFunction(call.name);
    if (!desc) {
        call.ctx.error(QStringLiteral("E0407"), QStringLiteral("unknown builtin function '%1'").arg(call.name), call.callSpan);
        return AbelValue::makeUnknown();
    }

    const int argc = static_cast<int>(call.args.size());
    if (!arityMatches(argc, desc->minArgs, desc->maxArgs)) {
        call.ctx.error(QStringLiteral("E0408"),
                       QStringLiteral("builtin function '%1' expects %2 argument(s), got %3")
                           .arg(call.name)
                           .arg(desc->minArgs == desc->maxArgs ? QString::number(desc->minArgs)
                                                               : QStringLiteral("%1..%2").arg(desc->minArgs).arg(desc->maxArgs))
                           .arg(argc),
                       call.callSpan);
        return AbelValue::makeUnknown();
    }

    return desc->runtime(call);
}

AbelValue BuiltinRegistry::callMethod(BuiltinMethodCall call) const
{
    const BuiltinMethodDesc* desc = findMethod(call.receiver.type(), call.name);
    if (!desc) {
        call.ctx.error(QStringLiteral("E0401"),
                       QStringLiteral("unknown builtin method '%1' for receiver type %2")
                           .arg(call.name, call.receiver.type().displayName()),
                       call.callSpan);
        return AbelValue::makeUnknown();
    }

    const int argc = static_cast<int>(call.args.size());
    if (!arityMatches(argc, desc->minArgs, desc->maxArgs)) {
        call.ctx.error(QStringLiteral("E0402"),
                       QStringLiteral("builtin method '%1' expects %2 argument(s), got %3")
                           .arg(call.name)
                           .arg(desc->minArgs == desc->maxArgs ? QString::number(desc->minArgs)
                                                               : QStringLiteral("%1..%2").arg(desc->minArgs).arg(desc->maxArgs))
                           .arg(argc),
                       call.callSpan);
        return AbelValue::makeUnknown();
    }

    return desc->runtime(call);
}

const BuiltinFunctionDesc* BuiltinRegistry::findFunction(const QString& name) const
{
    for (const auto& function : m_functions) {
        if (function.name == name)
            return &function;
    }
    return nullptr;
}

const BuiltinMethodDesc* BuiltinRegistry::findMethod(const AbelType& receiverType, const QString& name) const
{
    for (const auto& method : m_methods) {
        if (method.receiverKind == receiverType.kind && method.name == name)
            return &method;
    }
    return nullptr;
}

} // namespace abel
