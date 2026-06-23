#include "abelcore/builtin_registry.h"

#include <QTextStream>

#include <algorithm>
#include <cstddef>
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
    case TypeKind::I8:
    case TypeKind::I16:
    case TypeKind::I32:
    case TypeKind::I64:
        return QString::number(value.asInt());
    case TypeKind::U8:
    case TypeKind::U16:
    case TypeKind::U32:
    case TypeKind::U64:
        return QString::number(static_cast<quint64>(value.asInt()));
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

bool valuesEqual(const AbelValue& lhs, const AbelValue& rhs)
{
    if (lhs.type().kind == TypeKind::Any)
        return valuesEqual(lhs.asAny()->value, rhs);
    if (rhs.type().kind == TypeKind::Any)
        return valuesEqual(lhs, rhs.asAny()->value);
    if ((lhs.type().kind == TypeKind::Pointer && rhs.type().kind == TypeKind::Nullptr)
        || (lhs.type().kind == TypeKind::Nullptr && rhs.type().kind == TypeKind::Pointer)) {
        AbelLocation* ptr = lhs.type().kind == TypeKind::Pointer ? lhs.asPointer() : rhs.asPointer();
        return ptr == nullptr;
    }
    if (lhs.type().isNumeric() && rhs.type().isNumeric()) {
        if (lhs.type().kind == TypeKind::F64 || rhs.type().kind == TypeKind::F64)
            return lhs.asDouble() == rhs.asDouble();
        return lhs.asInt() == rhs.asInt();
    }
    if (lhs.type().kind != rhs.type().kind)
        return false;

    switch (lhs.type().kind) {
    case TypeKind::Void:
        return true;
    case TypeKind::Bool:
        return lhs.asBool() == rhs.asBool();
    case TypeKind::I8:
    case TypeKind::I16:
    case TypeKind::I32:
    case TypeKind::I64:
        return lhs.asInt() == rhs.asInt();
    case TypeKind::U8:
    case TypeKind::U16:
    case TypeKind::U32:
    case TypeKind::U64:
        return static_cast<quint64>(lhs.asInt()) == static_cast<quint64>(rhs.asInt());
    case TypeKind::F64:
        return lhs.asDouble() == rhs.asDouble();
    case TypeKind::Char:
        return lhs.asChar() == rhs.asChar();
    case TypeKind::Str:
        return lhs.asString() == rhs.asString();
    case TypeKind::Nullptr:
        return true;
    case TypeKind::Pointer:
        return lhs.asPointer() == rhs.asPointer();
    case TypeKind::Vector: {
        const auto lhsVector = lhs.asVector();
        const auto rhsVector = rhs.asVector();
        if (lhsVector->elements.size() != rhsVector->elements.size())
            return false;
        for (size_t i = 0; i < lhsVector->elements.size(); ++i) {
            if (!valuesEqual(lhsVector->elements[i], rhsVector->elements[i]))
                return false;
        }
        return true;
    }
    case TypeKind::Struct: {
        const auto lhsStruct = lhs.asStruct();
        const auto rhsStruct = rhs.asStruct();
        if (lhsStruct->typeName != rhsStruct->typeName || lhsStruct->fieldOrder != rhsStruct->fieldOrder)
            return false;
        for (const QString& field : lhsStruct->fieldOrder) {
            if (!valuesEqual(lhsStruct->fields.value(field), rhsStruct->fields.value(field)))
                return false;
        }
        return true;
    }
    case TypeKind::Function:
        return lhs.asFunction() == rhs.asFunction();
    case TypeKind::Reference:
    case TypeKind::Unknown:
        return false;
    }
    return false;
}

bool isOrderableType(const AbelType& type)
{
    return type.isNumeric()
        || type.kind == TypeKind::Bool
        || type.kind == TypeKind::Char
        || type.kind == TypeKind::Str;
}

bool valuesLess(const AbelValue& lhs, const AbelValue& rhs)
{
    if (lhs.type().isNumeric() && rhs.type().isNumeric()) {
        if (lhs.type().kind == TypeKind::F64 || rhs.type().kind == TypeKind::F64)
            return lhs.asDouble() < rhs.asDouble();
        if (lhs.type().isUnsignedInteger() || rhs.type().isUnsignedInteger())
            return static_cast<quint64>(lhs.asInt()) < static_cast<quint64>(rhs.asInt());
        return lhs.asInt() < rhs.asInt();
    }
    if (lhs.type().kind == TypeKind::Bool && rhs.type().kind == TypeKind::Bool)
        return !lhs.asBool() && rhs.asBool();
    if (lhs.type().kind == TypeKind::Char && rhs.type().kind == TypeKind::Char)
        return lhs.asChar().unicode() < rhs.asChar().unicode();
    if (lhs.type().kind == TypeKind::Str && rhs.type().kind == TypeKind::Str)
        return QString::compare(lhs.asString(), rhs.asString()) < 0;
    return false;
}

QString joinMessageSuffix(BuiltinFunctionCall& call, size_t start)
{
    QString detail;
    for (size_t i = start; i < call.args.size(); ++i) {
        const SourceSpan span = i < call.argSpans.size() ? call.argSpans[i] : call.callSpan;
        auto text = stringifyValue(call, call.args[i], span);
        if (!text.has_value())
            return {};
        detail += *text;
    }
    return detail.isEmpty() ? QString() : QStringLiteral(": ") + detail;
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

AbelValue builtinDebugBreak(BuiltinFunctionCall& call)
{
    call.ctx.error(QStringLiteral("E0596"), QStringLiteral("debug breakpoint"), call.callSpan);
    return AbelValue::makeUnknown();
}

AbelValue builtinDebugAssert(BuiltinFunctionCall& call)
{
    const AbelValue& condition = call.args[0];
    if (condition.type().kind != TypeKind::Bool) {
        const SourceSpan span = call.argSpans.empty() ? call.callSpan : call.argSpans[0];
        call.ctx.error(QStringLiteral("E0597"),
                       QStringLiteral("debug_assert condition must be bool, got %1").arg(condition.type().displayName()),
                       span);
        return AbelValue::makeUnknown();
    }
    if (condition.asBool())
        return AbelValue::makeVoid();

    QString message = QStringLiteral("debug assertion failed");
    if (call.args.size() > 1) {
        QString detail;
        for (size_t i = 1; i < call.args.size(); ++i) {
            const SourceSpan span = i < call.argSpans.size() ? call.argSpans[i] : call.callSpan;
            auto text = stringifyValue(call, call.args[i], span);
            if (!text.has_value())
                return AbelValue::makeUnknown();
            detail += *text;
        }
        if (!detail.isEmpty())
            message += QStringLiteral(": ") + detail;
    }

    call.ctx.error(QStringLiteral("E0598"), message, call.callSpan);
    return AbelValue::makeUnknown();
}

AbelValue builtinTestAssert(BuiltinFunctionCall& call)
{
    const AbelValue& condition = call.args[0];
    if (condition.type().kind != TypeKind::Bool) {
        const SourceSpan span = call.argSpans.empty() ? call.callSpan : call.argSpans[0];
        call.ctx.error(QStringLiteral("E0597"),
                       QStringLiteral("test_assert condition must be bool, got %1").arg(condition.type().displayName()),
                       span);
        return AbelValue::makeUnknown();
    }
    if (condition.asBool())
        return AbelValue::makeVoid();

    QString message = QStringLiteral("test assertion failed");
    if (call.args.size() > 1) {
        const QString suffix = joinMessageSuffix(call, 1);
        if (call.ctx.hasError())
            return AbelValue::makeUnknown();
        message += suffix;
    }
    call.ctx.error(QStringLiteral("E0599"), message, call.callSpan);
    return AbelValue::makeUnknown();
}

AbelValue builtinTestEqNe(BuiltinFunctionCall& call, bool wantEqual)
{
    const AbelValue& actual = call.args[0];
    const AbelValue& expected = call.args[1];
    const bool equal = valuesEqual(actual, expected);
    if (equal == wantEqual)
        return AbelValue::makeVoid();

    const SourceSpan actualSpan = call.argSpans.empty() ? call.callSpan : call.argSpans[0];
    const SourceSpan expectedSpan = call.argSpans.size() < 2 ? call.callSpan : call.argSpans[1];
    auto actualText = stringifyValue(call, actual, actualSpan);
    if (!actualText.has_value())
        return AbelValue::makeUnknown();
    auto expectedText = stringifyValue(call, expected, expectedSpan);
    if (!expectedText.has_value())
        return AbelValue::makeUnknown();

    QString message = wantEqual
        ? QStringLiteral("test_eq failed: expected %1, got %2").arg(*expectedText, *actualText)
        : QStringLiteral("test_ne failed: both were %1").arg(*actualText);
    const QString suffix = joinMessageSuffix(call, 2);
    if (call.ctx.hasError())
        return AbelValue::makeUnknown();
    message += suffix;
    call.ctx.error(QStringLiteral("E0599"), message, call.callSpan);
    return AbelValue::makeUnknown();
}

AbelValue builtinTestEq(BuiltinFunctionCall& call)
{
    return builtinTestEqNe(call, true);
}

AbelValue builtinTestNe(BuiltinFunctionCall& call)
{
    return builtinTestEqNe(call, false);
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
        const size_t target = static_cast<size_t>(count);
        if (target <= vector->elements.size()) {
            vector->elements.resize(target);
            return AbelValue::makeVoid();
        }
        vector->elements.reserve(target);
        while (vector->elements.size() < target) {
            AbelValue element = call.defaultValue
                ? call.defaultValue(vector->elementType, call.callSpan)
                : defaultValueForType(vector->elementType);
            if (call.ctx.hasError())
                return AbelValue::makeUnknown();
            if (element.type().kind == TypeKind::Unknown) {
                call.ctx.error(QStringLiteral("E0416"),
                               QStringLiteral("vector.resize cannot default-construct %1").arg(vector->elementType.displayName()),
                               call.callSpan);
                return AbelValue::makeUnknown();
            }
            vector->elements.push_back(element);
        }
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

AbelValue vectorInsert(BuiltinMethodCall& call)
{
    AbelValue indexValue = convertBuiltinArg(call, 0, makeType(TypeKind::I64));
    const AbelType& elementType = *call.receiver.type().pointee;
    AbelValue value = convertBuiltinArg(call, 1, elementType);
    if (call.ctx.hasError())
        return AbelValue::makeUnknown();
    const qint64 index = indexValue.asInt();
    auto vector = call.receiver.asVector();
    if (index < 0 || static_cast<size_t>(index) > vector->elements.size()) {
        call.ctx.error(QStringLiteral("E0419"),
                       QStringLiteral("vector.insert index out of range"),
                       call.argSpans.empty() ? call.callSpan : call.argSpans[0]);
        return AbelValue::makeUnknown();
    }
    try {
        vector->elements.insert(vector->elements.begin() + static_cast<std::ptrdiff_t>(index), value);
    } catch (const std::exception& e) {
        call.ctx.error(QStringLiteral("E0420"),
                       QStringLiteral("vector.insert failed: %1").arg(QString::fromLocal8Bit(e.what())),
                       call.callSpan);
        return AbelValue::makeUnknown();
    }
    return AbelValue::makeVoid();
}

AbelValue vectorErase(BuiltinMethodCall& call)
{
    AbelValue indexValue = convertBuiltinArg(call, 0, makeType(TypeKind::I64));
    if (call.ctx.hasError())
        return AbelValue::makeUnknown();
    const qint64 index = indexValue.asInt();
    auto vector = call.receiver.asVector();
    if (index < 0 || static_cast<size_t>(index) >= vector->elements.size()) {
        call.ctx.error(QStringLiteral("E0421"),
                       QStringLiteral("vector.erase index out of range"),
                       call.argSpans.empty() ? call.callSpan : call.argSpans[0]);
        return AbelValue::makeUnknown();
    }
    AbelValue removed = vector->elements[static_cast<size_t>(index)];
    vector->elements.erase(vector->elements.begin() + static_cast<std::ptrdiff_t>(index));
    return removed;
}

AbelValue vectorFind(BuiltinMethodCall& call)
{
    const AbelType& elementType = *call.receiver.type().pointee;
    AbelValue needle = convertBuiltinArg(call, 0, elementType);
    if (call.ctx.hasError())
        return AbelValue::makeUnknown();
    const auto vector = call.receiver.asVector();
    for (size_t i = 0; i < vector->elements.size(); ++i) {
        if (valuesEqual(vector->elements[i], needle))
            return AbelValue::makeInt(static_cast<qint64>(i), TypeKind::I32);
    }
    return AbelValue::makeInt(-1, TypeKind::I32);
}

AbelValue vectorSort(BuiltinMethodCall& call)
{
    auto vector = call.receiver.asVector();
    if (!isOrderableType(vector->elementType)) {
        call.ctx.error(QStringLiteral("E0422"),
                       QStringLiteral("vector.sort requires orderable element type, got %1")
                           .arg(vector->elementType.displayName()),
                       call.callSpan);
        return AbelValue::makeUnknown();
    }
    std::sort(vector->elements.begin(), vector->elements.end(), valuesLess);
    return AbelValue::makeVoid();
}

AbelValue stringLen(BuiltinMethodCall& call)
{
    return AbelValue::makeInt(call.receiver.asString().size(), TypeKind::I32);
}

AbelValue stringEmpty(BuiltinMethodCall& call)
{
    return AbelValue::makeBool(call.receiver.asString().isEmpty());
}

AbelValue stringContains(BuiltinMethodCall& call)
{
    AbelValue needle = convertBuiltinArg(call, 0, makeType(TypeKind::Str));
    if (call.ctx.hasError())
        return AbelValue::makeUnknown();
    return AbelValue::makeBool(call.receiver.asString().contains(needle.asString()));
}

AbelValue stringFind(BuiltinMethodCall& call)
{
    AbelValue needle = convertBuiltinArg(call, 0, makeType(TypeKind::Str));
    if (call.ctx.hasError())
        return AbelValue::makeUnknown();
    return AbelValue::makeInt(call.receiver.asString().indexOf(needle.asString()), TypeKind::I32);
}

AbelValue stringSlice(BuiltinMethodCall& call)
{
    AbelValue startValue = convertBuiltinArg(call, 0, makeType(TypeKind::I64));
    AbelValue lenValue = convertBuiltinArg(call, 1, makeType(TypeKind::I64));
    if (call.ctx.hasError())
        return AbelValue::makeUnknown();
    const qint64 start = startValue.asInt();
    const qint64 len = lenValue.asInt();
    if (start < 0 || len < 0) {
        const SourceSpan span = start < 0
            ? (call.argSpans.empty() ? call.callSpan : call.argSpans[0])
            : (call.argSpans.size() < 2 ? call.callSpan : call.argSpans[1]);
        call.ctx.error(QStringLiteral("E0418"),
                       QStringLiteral("str.%1 expects non-negative start and length").arg(call.name),
                       span);
        return AbelValue::makeUnknown();
    }
    return AbelValue::makeString(call.receiver.asString().mid(static_cast<qsizetype>(start), static_cast<qsizetype>(len)));
}

AbelValue stringReplace(BuiltinMethodCall& call)
{
    AbelValue before = convertBuiltinArg(call, 0, makeType(TypeKind::Str));
    AbelValue after = convertBuiltinArg(call, 1, makeType(TypeKind::Str));
    if (call.ctx.hasError())
        return AbelValue::makeUnknown();
    QString copy = call.receiver.asString();
    copy.replace(before.asString(), after.asString());
    return AbelValue::makeString(copy);
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
    registry.registerMethod({TypeKind::Vector, QStringLiteral("insert"), 2, 2, true, vectorInsert, QStringLiteral("vector insert at index")});
    registry.registerMethod({TypeKind::Vector, QStringLiteral("erase"), 1, 1, true, vectorErase, QStringLiteral("vector erase at index and return removed value")});
    registry.registerMethod({TypeKind::Vector, QStringLiteral("find"), 1, 1, false, vectorFind, QStringLiteral("vector find, -1 when absent")});
    registry.registerMethod({TypeKind::Vector, QStringLiteral("sort"), 0, 0, true, vectorSort, QStringLiteral("vector sort orderable elements")});

    registry.registerMethod({TypeKind::Str, QStringLiteral("len"), 0, 0, false, stringLen, QStringLiteral("string length")});
    registry.registerMethod({TypeKind::Str, QStringLiteral("empty"), 0, 0, false, stringEmpty, QStringLiteral("string empty test")});
    registry.registerMethod({TypeKind::Str, QStringLiteral("contains"), 1, 1, false, stringContains, QStringLiteral("string contains")});
    registry.registerMethod({TypeKind::Str, QStringLiteral("find"), 1, 1, false, stringFind, QStringLiteral("string find, -1 when absent")});
    registry.registerMethod({TypeKind::Str, QStringLiteral("substr"), 2, 2, false, stringSlice, QStringLiteral("string substring")});
    registry.registerMethod({TypeKind::Str, QStringLiteral("slice"), 2, 2, false, stringSlice, QStringLiteral("string slice alias")});
    registry.registerMethod({TypeKind::Str, QStringLiteral("replace"), 2, 2, false, stringReplace, QStringLiteral("string replace all")});

    registry.registerFunction({QStringLiteral("to_str"), 1, 1, false, builtinToStr, QStringLiteral("stringify one value")});
    registry.registerFunction({QStringLiteral("build_string"), 0, -1, true, builtinBuildString, QStringLiteral("concatenate stringified values")});
    registry.registerFunction({QStringLiteral("print"), 0, -1, true, builtinPrint, QStringLiteral("print stringified values")});
    registry.registerFunction({QStringLiteral("println"), 0, -1, true, builtinPrintln, QStringLiteral("print stringified values plus newline")});
    registry.registerFunction({QStringLiteral("str_to_chars"), 1, 1, false, builtinStrToChars, QStringLiteral("convert str to vector<char>")});
    registry.registerFunction({QStringLiteral("chars_to_str"), 1, 1, false, builtinCharsToStr, QStringLiteral("convert vector<char> to str")});
    registry.registerFunction({QStringLiteral("debug_break"), 0, 0, false, builtinDebugBreak, QStringLiteral("emit a debug breakpoint diagnostic")});
    registry.registerFunction({QStringLiteral("debug_assert"), 1, -1, true, builtinDebugAssert, QStringLiteral("emit a diagnostic when condition is false")});
    registry.registerFunction({QStringLiteral("test_assert"), 1, -1, true, builtinTestAssert, QStringLiteral("fail current test when condition is false")});
    registry.registerFunction({QStringLiteral("test_eq"), 2, -1, true, builtinTestEq, QStringLiteral("fail current test when values are not equal")});
    registry.registerFunction({QStringLiteral("test_ne"), 2, -1, true, builtinTestNe, QStringLiteral("fail current test when values are equal")});

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
    if (desc->mutatesReceiver && call.receiverLocation && call.receiverLocation->isReadOnly) {
        call.ctx.error(QStringLiteral("E0417"),
                       QStringLiteral("builtin method '%1' requires mutable receiver").arg(call.name),
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
