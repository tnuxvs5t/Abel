#include "abelcore/builtin_registry.h"

#include <QTextStream>

#include <exception>

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

std::optional<QString> stringifyValue(BuiltinFunctionCall& call, const AbelValue& value, const SourceSpan& span)
{
    if (call.stringify) {
        auto custom = call.stringify(value, span);
        if (custom.has_value())
            return custom;
        if (call.ctx.hasError())
            return std::nullopt;
    }

    switch (value.type().kind) {
    case TypeKind::Void:
        return QString();
    case TypeKind::Bool:
        return value.asBool() ? QStringLiteral("true") : QStringLiteral("false");
    case TypeKind::I32:
    case TypeKind::I64:
        return QString::number(value.asInt());
    case TypeKind::F64:
        return QString::number(value.asDouble(), 'g', 16);
    case TypeKind::Char:
        return QString(value.asChar());
    case TypeKind::Str:
        return value.asString();
    case TypeKind::Any:
        return stringifyValue(call, value.asAny()->value, span);
    case TypeKind::Nullptr:
        return QStringLiteral("nullptr");
    case TypeKind::Pointer:
        return value.asPointer() ? QStringLiteral("<ptr>") : QStringLiteral("nullptr");
    case TypeKind::Vector: {
        QString out = QStringLiteral("[");
        const auto vector = value.asVector();
        for (size_t i = 0; i < vector->elements.size(); ++i) {
            if (i > 0)
                out += QStringLiteral(", ");
            auto element = stringifyValue(call, vector->elements[i], span);
            if (!element.has_value())
                return std::nullopt;
            out += *element;
        }
        out += QStringLiteral("]");
        return out;
    }
    case TypeKind::Reference:
    case TypeKind::Unknown:
    case TypeKind::Struct:
    case TypeKind::Function:
        call.ctx.error(QStringLiteral("E0415"),
                       QStringLiteral("cannot stringify %1").arg(value.type().displayName()),
                       span);
        return std::nullopt;
    }
    call.ctx.error(QStringLiteral("E0415"),
                   QStringLiteral("cannot stringify %1").arg(value.type().displayName()),
                   span);
    return std::nullopt;
}

AbelValue builtinToStr(BuiltinFunctionCall& call)
{
    auto text = stringifyValue(call, call.args[0], call.argSpans.empty() ? call.callSpan : call.argSpans[0]);
    if (!text.has_value())
        return AbelValue::makeUnknown();
    return AbelValue::makeString(*text);
}

AbelValue builtinBuildString(BuiltinFunctionCall& call)
{
    QString out;
    for (size_t i = 0; i < call.args.size(); ++i) {
        const SourceSpan span = i < call.argSpans.size() ? call.argSpans[i] : call.callSpan;
        auto text = stringifyValue(call, call.args[i], span);
        if (!text.has_value())
            return AbelValue::makeUnknown();
        out += *text;
    }
    return AbelValue::makeString(out);
}

AbelValue builtinPrint(BuiltinFunctionCall& call)
{
    QTextStream out(stdout);
    for (size_t i = 0; i < call.args.size(); ++i) {
        const SourceSpan span = i < call.argSpans.size() ? call.argSpans[i] : call.callSpan;
        auto text = stringifyValue(call, call.args[i], span);
        if (!text.has_value())
            return AbelValue::makeUnknown();
        out << *text;
    }
    out.flush();
    return AbelValue::makeVoid();
}

AbelValue builtinPrintln(BuiltinFunctionCall& call)
{
    QTextStream out(stdout);
    for (size_t i = 0; i < call.args.size(); ++i) {
        const SourceSpan span = i < call.argSpans.size() ? call.argSpans[i] : call.callSpan;
        auto text = stringifyValue(call, call.args[i], span);
        if (!text.has_value())
            return AbelValue::makeUnknown();
        out << *text;
    }
    out << Qt::endl;
    return AbelValue::makeVoid();
}

AbelValue builtinStrToChars(BuiltinFunctionCall& call)
{
    const AbelValue& value = call.args[0];
    if (value.type().kind != TypeKind::Str) {
        const SourceSpan span = call.argSpans.empty() ? call.callSpan : call.argSpans[0];
        call.ctx.error(QStringLiteral("E0413"),
                       QStringLiteral("str_to_chars expects str, got %1").arg(value.type().displayName()),
                       span);
        return AbelValue::makeUnknown();
    }
    std::vector<AbelValue> chars;
    const QString s = value.asString();
    chars.reserve(static_cast<size_t>(s.size()));
    for (QChar ch : s)
        chars.push_back(AbelValue::makeChar(ch));
    return AbelValue::makeVector(makeType(TypeKind::Char), std::move(chars));
}

AbelValue builtinCharsToStr(BuiltinFunctionCall& call)
{
    const AbelValue& value = call.args[0];
    const AbelType charVector = makeVectorType(makeType(TypeKind::Char));
    if (!canAssignValue(charVector, value.type())) {
        const SourceSpan span = call.argSpans.empty() ? call.callSpan : call.argSpans[0];
        call.ctx.error(QStringLiteral("E0414"),
                       QStringLiteral("chars_to_str expects vector<char>, got %1").arg(value.type().displayName()),
                       span);
        return AbelValue::makeUnknown();
    }
    QString out;
    const auto vector = value.asVector();
    out.reserve(static_cast<qsizetype>(vector->elements.size()));
    for (const auto& element : vector->elements)
        out.push_back(element.asChar());
    return AbelValue::makeString(out);
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

AbelValue vectorClear(BuiltinMethodCall& call)
{
    call.receiver.asVector()->elements.clear();
    return AbelValue::makeVoid();
}

AbelValue vectorReserve(BuiltinMethodCall& call)
{
    AbelValue value = convertBuiltinArg(call, 0, makeType(TypeKind::I64));
    if (call.ctx.hasError())
        return AbelValue::makeUnknown();
    const qint64 count = value.asInt();
    if (count < 0) {
        call.ctx.error(QStringLiteral("E0409"), QStringLiteral("vector.reserve expects non-negative size"), call.argSpans.empty() ? call.callSpan : call.argSpans[0]);
        return AbelValue::makeUnknown();
    }
    try {
        call.receiver.asVector()->elements.reserve(static_cast<size_t>(count));
    } catch (const std::exception& e) {
        call.ctx.error(QStringLiteral("E0410"), QStringLiteral("vector.reserve failed: %1").arg(QString::fromLocal8Bit(e.what())), call.callSpan);
        return AbelValue::makeUnknown();
    }
    return AbelValue::makeVoid();
}

AbelValue vectorResize(BuiltinMethodCall& call)
{
    AbelValue value = convertBuiltinArg(call, 0, makeType(TypeKind::I64));
    if (call.ctx.hasError())
        return AbelValue::makeUnknown();
    const qint64 count = value.asInt();
    if (count < 0) {
        call.ctx.error(QStringLiteral("E0411"), QStringLiteral("vector.resize expects non-negative size"), call.argSpans.empty() ? call.callSpan : call.argSpans[0]);
        return AbelValue::makeUnknown();
    }
    auto vector = call.receiver.asVector();
    try {
        vector->elements.resize(static_cast<size_t>(count), defaultValueForType(vector->elementType));
    } catch (const std::exception& e) {
        call.ctx.error(QStringLiteral("E0412"), QStringLiteral("vector.resize failed: %1").arg(QString::fromLocal8Bit(e.what())), call.callSpan);
        return AbelValue::makeUnknown();
    }
    return AbelValue::makeVoid();
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
    registry.registerMethod({TypeKind::Vector, QStringLiteral("clear"), 0, 0, true, vectorClear, QStringLiteral("vector clear")});
    registry.registerMethod({TypeKind::Vector, QStringLiteral("reserve"), 1, 1, true, vectorReserve, QStringLiteral("vector reserve")});
    registry.registerMethod({TypeKind::Vector, QStringLiteral("resize"), 1, 1, true, vectorResize, QStringLiteral("vector resize")});
    registry.registerMethod({TypeKind::Vector, QStringLiteral("front"), 0, 0, false, vectorFront, QStringLiteral("vector front")});
    registry.registerMethod({TypeKind::Vector, QStringLiteral("back"), 0, 0, false, vectorBack, QStringLiteral("vector back")});

    registry.registerFunction({QStringLiteral("to_str"), 1, 1, false, builtinToStr, QStringLiteral("stringify one value")});
    registry.registerFunction({QStringLiteral("build_string"), 0, -1, true, builtinBuildString, QStringLiteral("concatenate stringified values")});
    registry.registerFunction({QStringLiteral("print"), 0, -1, true, builtinPrint, QStringLiteral("print stringified values")});
    registry.registerFunction({QStringLiteral("println"), 0, -1, true, builtinPrintln, QStringLiteral("print stringified values plus newline")});
    registry.registerFunction({QStringLiteral("str_to_chars"), 1, 1, false, builtinStrToChars, QStringLiteral("convert str to vector<char>")});
    registry.registerFunction({QStringLiteral("chars_to_str"), 1, 1, false, builtinCharsToStr, QStringLiteral("convert vector<char> to str")});

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
