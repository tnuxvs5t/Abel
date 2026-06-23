#include "abelcore/builtin_registry.h"

#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <exception>
#include <iterator>
#include <limits>
#include <numeric>

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

TypeKind integerTypeForWidth(int width, bool unsignedResult)
{
    if (width <= 8)
        return unsignedResult ? TypeKind::U8 : TypeKind::I8;
    if (width <= 16)
        return unsignedResult ? TypeKind::U16 : TypeKind::I16;
    if (width <= 32)
        return unsignedResult ? TypeKind::U32 : TypeKind::I32;
    return unsignedResult ? TypeKind::U64 : TypeKind::I64;
}

AbelType numericResultType(const AbelType& lhs, const AbelType& rhs)
{
    if (lhs.kind == TypeKind::F64 || rhs.kind == TypeKind::F64)
        return makeType(TypeKind::F64);
    const int width = std::max({32, lhs.integerBitWidth(), rhs.integerBitWidth()});
    const bool unsignedResult = lhs.isUnsignedInteger() || rhs.isUnsignedInteger();
    return makeType(integerTypeForWidth(width, unsignedResult));
}

std::optional<AbelValue> requireNumericArg(BuiltinFunctionCall& call, int index)
{
    const AbelValue& value = call.args[static_cast<size_t>(index)];
    if (!value.type().isNumeric()) {
        const SourceSpan span = index < static_cast<int>(call.argSpans.size()) ? call.argSpans[static_cast<size_t>(index)] : call.callSpan;
        call.ctx.error(QStringLiteral("E0431"),
                       QStringLiteral("%1 expects numeric argument, got %2")
                           .arg(call.name, value.type().displayName()),
                       span);
        return std::nullopt;
    }
    return value;
}

std::optional<AbelValue> requireIntegerArg(BuiltinFunctionCall& call, int index)
{
    const AbelValue& value = call.args[static_cast<size_t>(index)];
    if (!value.type().isInteger()) {
        const SourceSpan span = index < static_cast<int>(call.argSpans.size()) ? call.argSpans[static_cast<size_t>(index)] : call.callSpan;
        call.ctx.error(QStringLiteral("E0435"),
                       QStringLiteral("%1 expects integer argument, got %2")
                           .arg(call.name, value.type().displayName()),
                       span);
        return std::nullopt;
    }
    return value;
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

AbelValue builtinMath(BuiltinFunctionCall& call)
{
    const QString& name = call.name;

    if (name == QStringLiteral("abs")) {
        auto value = requireNumericArg(call, 0);
        if (!value.has_value())
            return AbelValue::makeUnknown();
        if (value->type().kind == TypeKind::F64)
            return AbelValue::makeDouble(std::fabs(value->asDouble()));
        if (value->type().isUnsignedInteger())
            return *value;
        const qint64 raw = value->asInt();
        return AbelValue::makeInt(raw < 0 ? -raw : raw, value->type().kind);
    }

    if (name == QStringLiteral("sqrt")
        || name == QStringLiteral("floor")
        || name == QStringLiteral("ceil")
        || name == QStringLiteral("round")
        || name == QStringLiteral("trunc")
        || name == QStringLiteral("sin")
        || name == QStringLiteral("cos")
        || name == QStringLiteral("tan")
        || name == QStringLiteral("asin")
        || name == QStringLiteral("acos")
        || name == QStringLiteral("atan")
        || name == QStringLiteral("exp")
        || name == QStringLiteral("log")
        || name == QStringLiteral("log10")) {
        auto value = requireNumericArg(call, 0);
        if (!value.has_value())
            return AbelValue::makeUnknown();
        const double x = value->asDouble();
        if (name == QStringLiteral("sqrt"))
            return AbelValue::makeDouble(std::sqrt(x));
        if (name == QStringLiteral("floor"))
            return AbelValue::makeDouble(std::floor(x));
        if (name == QStringLiteral("ceil"))
            return AbelValue::makeDouble(std::ceil(x));
        if (name == QStringLiteral("round"))
            return AbelValue::makeDouble(std::round(x));
        if (name == QStringLiteral("trunc"))
            return AbelValue::makeDouble(std::trunc(x));
        if (name == QStringLiteral("sin"))
            return AbelValue::makeDouble(std::sin(x));
        if (name == QStringLiteral("cos"))
            return AbelValue::makeDouble(std::cos(x));
        if (name == QStringLiteral("tan"))
            return AbelValue::makeDouble(std::tan(x));
        if (name == QStringLiteral("asin"))
            return AbelValue::makeDouble(std::asin(x));
        if (name == QStringLiteral("acos"))
            return AbelValue::makeDouble(std::acos(x));
        if (name == QStringLiteral("atan"))
            return AbelValue::makeDouble(std::atan(x));
        if (name == QStringLiteral("exp"))
            return AbelValue::makeDouble(std::exp(x));
        if (name == QStringLiteral("log"))
            return AbelValue::makeDouble(std::log(x));
        return AbelValue::makeDouble(std::log10(x));
    }

    if (name == QStringLiteral("pow") || name == QStringLiteral("atan2")) {
        auto lhs = requireNumericArg(call, 0);
        auto rhs = requireNumericArg(call, 1);
        if (!lhs.has_value() || !rhs.has_value())
            return AbelValue::makeUnknown();
        if (name == QStringLiteral("atan2"))
            return AbelValue::makeDouble(std::atan2(lhs->asDouble(), rhs->asDouble()));
        return AbelValue::makeDouble(std::pow(lhs->asDouble(), rhs->asDouble()));
    }

    if (name == QStringLiteral("gcd") || name == QStringLiteral("lcm")) {
        auto lhs = requireIntegerArg(call, 0);
        auto rhs = requireIntegerArg(call, 1);
        if (!lhs.has_value() || !rhs.has_value())
            return AbelValue::makeUnknown();
        const AbelType outType = numericResultType(lhs->type(), rhs->type());
        const qint64 result = name == QStringLiteral("gcd")
            ? std::gcd(lhs->asInt(), rhs->asInt())
            : std::lcm(lhs->asInt(), rhs->asInt());
        return AbelValue::makeInt(result, outType.kind);
    }

    if (name == QStringLiteral("min") || name == QStringLiteral("max")) {
        auto lhs = requireNumericArg(call, 0);
        auto rhs = requireNumericArg(call, 1);
        if (!lhs.has_value() || !rhs.has_value())
            return AbelValue::makeUnknown();
        const AbelType outType = numericResultType(lhs->type(), rhs->type());
        const bool takeLhs = name == QStringLiteral("min")
            ? valuesLess(*lhs, *rhs)
            : valuesLess(*rhs, *lhs);
        return convertValue(takeLhs ? *lhs : *rhs, outType);
    }

    if (name == QStringLiteral("clamp")) {
        auto value = requireNumericArg(call, 0);
        auto low = requireNumericArg(call, 1);
        auto high = requireNumericArg(call, 2);
        if (!value.has_value() || !low.has_value() || !high.has_value())
            return AbelValue::makeUnknown();
        if (valuesLess(*high, *low)) {
            call.ctx.error(QStringLiteral("E0432"),
                           QStringLiteral("clamp lower bound is greater than upper bound"),
                           call.callSpan);
            return AbelValue::makeUnknown();
        }
        const AbelType outType = numericResultType(numericResultType(value->type(), low->type()), high->type());
        const AbelValue& chosen = valuesLess(*value, *low)
            ? *low
            : (valuesLess(*high, *value) ? *high : *value);
        return convertValue(chosen, outType);
    }

    call.ctx.error(QStringLiteral("E0433"), QStringLiteral("unknown math builtin '%1'").arg(name), call.callSpan);
    return AbelValue::makeUnknown();
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

std::optional<AbelValue> scanTokenToValue(BuiltinFunctionCall& call, const AbelType& target, const SourceSpan& span)
{
    if (!call.readToken) {
        call.ctx.error(QStringLiteral("E0423"), QStringLiteral("scan input is not configured"), span);
        return std::nullopt;
    }

    auto token = call.readToken(span);
    if (!token.has_value())
        return std::nullopt;

    bool ok = false;
    switch (target.kind) {
    case TypeKind::Bool:
        if (*token == QStringLiteral("true") || *token == QStringLiteral("1"))
            return AbelValue::makeBool(true);
        if (*token == QStringLiteral("false") || *token == QStringLiteral("0"))
            return AbelValue::makeBool(false);
        call.ctx.error(QStringLiteral("E0424"),
                       QStringLiteral("scan cannot parse bool from '%1'").arg(*token),
                       span);
        return std::nullopt;
    case TypeKind::I8:
    case TypeKind::I16:
    case TypeKind::I32:
    case TypeKind::I64:
    case TypeKind::U8:
    case TypeKind::U16:
    case TypeKind::U32:
    case TypeKind::U64: {
        const qint64 value = target.isUnsignedInteger()
            ? static_cast<qint64>(token->toULongLong(&ok, 10))
            : token->toLongLong(&ok, 10);
        if (!ok) {
            call.ctx.error(QStringLiteral("E0424"),
                           QStringLiteral("scan cannot parse %1 from '%2'")
                               .arg(target.displayName(), *token),
                           span);
            return std::nullopt;
        }
        return AbelValue::makeInt(value, target.kind);
    }
    case TypeKind::F64: {
        const double value = token->toDouble(&ok);
        if (!ok) {
            call.ctx.error(QStringLiteral("E0424"),
                           QStringLiteral("scan cannot parse f64 from '%1'").arg(*token),
                           span);
            return std::nullopt;
        }
        return AbelValue::makeDouble(value);
    }
    case TypeKind::Char:
        if (token->size() != 1) {
            call.ctx.error(QStringLiteral("E0424"),
                           QStringLiteral("scan cannot parse char from '%1'").arg(*token),
                           span);
            return std::nullopt;
        }
        return AbelValue::makeChar(token->front());
    case TypeKind::Str:
        return AbelValue::makeString(*token);
    case TypeKind::Any:
        return AbelValue::makeAny(AbelValue::makeString(*token));
    default:
        call.ctx.error(QStringLiteral("E0425"),
                       QStringLiteral("scan does not support target type %1").arg(target.displayName()),
                       span);
        return std::nullopt;
    }
}

AbelValue builtinScan(BuiltinFunctionCall& call)
{
    for (size_t i = 0; i < call.args.size(); ++i) {
        const AbelValue& pointer = call.args[i];
        const SourceSpan span = i < call.argSpans.size() ? call.argSpans[i] : call.callSpan;
        if (!pointer.type().isPointer() || !pointer.type().pointee) {
            call.ctx.error(QStringLiteral("E0426"),
                           QStringLiteral("scan argument must be pointer, got %1").arg(pointer.type().displayName()),
                           span);
            return AbelValue::makeUnknown();
        }
        AbelLocation* target = pointer.asPointer();
        if (!target) {
            call.ctx.error(QStringLiteral("E0427"), QStringLiteral("scan cannot write through nullptr"), span);
            return AbelValue::makeUnknown();
        }
        if (target->isReadOnly || pointer.type().pointee->isConst) {
            call.ctx.error(QStringLiteral("E0428"), QStringLiteral("scan cannot write through readonly pointer"), span);
            return AbelValue::makeUnknown();
        }
        auto value = scanTokenToValue(call, *pointer.type().pointee, span);
        if (!value.has_value())
            return AbelValue::makeUnknown();
        target->write(convertValue(*value, *pointer.type().pointee));
    }
    return AbelValue::makeVoid();
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

AbelValue builtinTestClose(BuiltinFunctionCall& call)
{
    const AbelValue& actual = call.args[0];
    const AbelValue& expected = call.args[1];
    const AbelValue& eps = call.args[2];
    if (!actual.type().isNumeric() || !expected.type().isNumeric() || !eps.type().isNumeric()) {
        call.ctx.error(QStringLiteral("E0597"),
                       QStringLiteral("test_close expects numeric actual, expected and eps"),
                       call.callSpan);
        return AbelValue::makeUnknown();
    }
    const double tolerance = eps.asDouble();
    if (tolerance < 0) {
        call.ctx.error(QStringLiteral("E0597"),
                       QStringLiteral("test_close eps must be non-negative"),
                       call.argSpans.size() < 3 ? call.callSpan : call.argSpans[2]);
        return AbelValue::makeUnknown();
    }
    if (std::fabs(actual.asDouble() - expected.asDouble()) <= tolerance)
        return AbelValue::makeVoid();

    const SourceSpan actualSpan = call.argSpans.empty() ? call.callSpan : call.argSpans[0];
    const SourceSpan expectedSpan = call.argSpans.size() < 2 ? call.callSpan : call.argSpans[1];
    auto actualText = stringifyValue(call, actual, actualSpan);
    if (!actualText.has_value())
        return AbelValue::makeUnknown();
    auto expectedText = stringifyValue(call, expected, expectedSpan);
    if (!expectedText.has_value())
        return AbelValue::makeUnknown();

    QString message = QStringLiteral("test_close failed: expected %1 +/- %2, got %3")
        .arg(*expectedText, QString::number(tolerance, 'g', 16), *actualText);
    const QString suffix = joinMessageSuffix(call, 3);
    if (call.ctx.hasError())
        return AbelValue::makeUnknown();
    message += suffix;
    call.ctx.error(QStringLiteral("E0599"), message, call.callSpan);
    return AbelValue::makeUnknown();
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

AbelValue vectorContains(BuiltinMethodCall& call)
{
    const AbelType& elementType = *call.receiver.type().pointee;
    AbelValue needle = convertBuiltinArg(call, 0, elementType);
    if (call.ctx.hasError())
        return AbelValue::makeUnknown();
    const auto vector = call.receiver.asVector();
    for (const AbelValue& element : vector->elements) {
        if (valuesEqual(element, needle))
            return AbelValue::makeBool(true);
    }
    return AbelValue::makeBool(false);
}

AbelValue vectorCount(BuiltinMethodCall& call)
{
    const AbelType& elementType = *call.receiver.type().pointee;
    AbelValue needle = convertBuiltinArg(call, 0, elementType);
    if (call.ctx.hasError())
        return AbelValue::makeUnknown();
    const auto vector = call.receiver.asVector();
    qint64 count = 0;
    for (const AbelValue& element : vector->elements) {
        if (valuesEqual(element, needle))
            ++count;
    }
    return AbelValue::makeInt(count, TypeKind::I32);
}

AbelValue vectorExtend(BuiltinMethodCall& call)
{
    const AbelType& receiverType = call.receiver.type();
    AbelValue otherValue = convertBuiltinArg(call, 0, receiverType);
    if (call.ctx.hasError())
        return AbelValue::makeUnknown();
    auto vector = call.receiver.asVector();
    const auto other = otherValue.asVector();
    vector->elements.insert(vector->elements.end(), other->elements.begin(), other->elements.end());
    return AbelValue::makeVoid();
}

AbelValue vectorSlice(BuiltinMethodCall& call)
{
    AbelValue startValue = convertBuiltinArg(call, 0, makeType(TypeKind::I64));
    AbelValue lenValue = convertBuiltinArg(call, 1, makeType(TypeKind::I64));
    if (call.ctx.hasError())
        return AbelValue::makeUnknown();
    const qint64 start = startValue.asInt();
    const qint64 len = lenValue.asInt();
    const auto vector = call.receiver.asVector();
    if (start < 0 || len < 0 || static_cast<size_t>(start) > vector->elements.size()) {
        call.ctx.error(QStringLiteral("E0437"),
                       QStringLiteral("vector.slice expects non-negative start/length within vector bounds"),
                       call.callSpan);
        return AbelValue::makeUnknown();
    }
    const size_t from = static_cast<size_t>(start);
    const size_t available = vector->elements.size() - from;
    const size_t count = std::min(static_cast<size_t>(len), available);
    std::vector<AbelValue> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i)
        out.push_back(vector->elements[from + i]);
    return AbelValue::makeVector(vector->elementType, std::move(out));
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

AbelValue vectorReverse(BuiltinMethodCall& call)
{
    auto vector = call.receiver.asVector();
    std::reverse(vector->elements.begin(), vector->elements.end());
    return AbelValue::makeVoid();
}

AbelValue vectorUnique(BuiltinMethodCall& call)
{
    auto vector = call.receiver.asVector();
    auto last = std::unique(vector->elements.begin(), vector->elements.end(), valuesEqual);
    vector->elements.erase(last, vector->elements.end());
    return AbelValue::makeVoid();
}

std::optional<AbelValue> vectorOrderableNeedle(BuiltinMethodCall& call)
{
    auto vector = call.receiver.asVector();
    if (!isOrderableType(vector->elementType)) {
        call.ctx.error(QStringLiteral("E0422"),
                       QStringLiteral("vector.%1 requires orderable element type, got %2")
                           .arg(call.name, vector->elementType.displayName()),
                       call.callSpan);
        return std::nullopt;
    }
    AbelValue needle = convertBuiltinArg(call, 0, vector->elementType);
    if (call.ctx.hasError())
        return std::nullopt;
    return needle;
}

AbelValue vectorLowerBound(BuiltinMethodCall& call)
{
    auto needle = vectorOrderableNeedle(call);
    if (!needle.has_value())
        return AbelValue::makeUnknown();
    const auto vector = call.receiver.asVector();
    const auto it = std::lower_bound(vector->elements.begin(), vector->elements.end(), *needle, valuesLess);
    return AbelValue::makeInt(static_cast<qint64>(std::distance(vector->elements.begin(), it)), TypeKind::I32);
}

AbelValue vectorUpperBound(BuiltinMethodCall& call)
{
    auto needle = vectorOrderableNeedle(call);
    if (!needle.has_value())
        return AbelValue::makeUnknown();
    const auto vector = call.receiver.asVector();
    const auto it = std::upper_bound(vector->elements.begin(), vector->elements.end(), *needle, valuesLess);
    return AbelValue::makeInt(static_cast<qint64>(std::distance(vector->elements.begin(), it)), TypeKind::I32);
}

AbelValue vectorBinarySearch(BuiltinMethodCall& call)
{
    auto needle = vectorOrderableNeedle(call);
    if (!needle.has_value())
        return AbelValue::makeUnknown();
    const auto vector = call.receiver.asVector();
    return AbelValue::makeBool(std::binary_search(vector->elements.begin(), vector->elements.end(), *needle, valuesLess));
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

AbelValue stringStartsWith(BuiltinMethodCall& call)
{
    AbelValue prefix = convertBuiltinArg(call, 0, makeType(TypeKind::Str));
    if (call.ctx.hasError())
        return AbelValue::makeUnknown();
    return AbelValue::makeBool(call.receiver.asString().startsWith(prefix.asString()));
}

AbelValue stringEndsWith(BuiltinMethodCall& call)
{
    AbelValue suffix = convertBuiltinArg(call, 0, makeType(TypeKind::Str));
    if (call.ctx.hasError())
        return AbelValue::makeUnknown();
    return AbelValue::makeBool(call.receiver.asString().endsWith(suffix.asString()));
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

AbelValue stringTrim(BuiltinMethodCall& call)
{
    return AbelValue::makeString(call.receiver.asString().trimmed());
}

AbelValue stringLower(BuiltinMethodCall& call)
{
    return AbelValue::makeString(call.receiver.asString().toLower());
}

AbelValue stringUpper(BuiltinMethodCall& call)
{
    return AbelValue::makeString(call.receiver.asString().toUpper());
}

AbelValue stringSplit(BuiltinMethodCall& call)
{
    AbelValue sep = convertBuiltinArg(call, 0, makeType(TypeKind::Str));
    if (call.ctx.hasError())
        return AbelValue::makeUnknown();
    if (sep.asString().isEmpty()) {
        call.ctx.error(QStringLiteral("E0434"),
                       QStringLiteral("str.split separator must not be empty"),
                       call.argSpans.empty() ? call.callSpan : call.argSpans[0]);
        return AbelValue::makeUnknown();
    }
    std::vector<AbelValue> parts;
    const QStringList split = call.receiver.asString().split(sep.asString(), Qt::KeepEmptyParts);
    parts.reserve(static_cast<size_t>(split.size()));
    for (const QString& part : split)
        parts.push_back(AbelValue::makeString(part));
    return AbelValue::makeVector(makeType(TypeKind::Str), std::move(parts));
}

AbelValue stringJoin(BuiltinMethodCall& call)
{
    const AbelType strVectorType = makeVectorType(makeType(TypeKind::Str));
    AbelValue values = convertBuiltinArg(call, 0, strVectorType);
    if (call.ctx.hasError())
        return AbelValue::makeUnknown();

    QStringList parts;
    const auto vector = values.asVector();
    parts.reserve(static_cast<qsizetype>(vector->elements.size()));
    for (const AbelValue& element : vector->elements)
        parts.push_back(element.asString());
    return AbelValue::makeString(parts.join(call.receiver.asString()));
}

AbelValue stringParseInt(BuiltinMethodCall& call)
{
    bool ok = false;
    const qlonglong value = call.receiver.asString().trimmed().toLongLong(&ok, 10);
    if (!ok
        || value < std::numeric_limits<qint32>::min()
        || value > std::numeric_limits<qint32>::max()) {
        call.ctx.error(QStringLiteral("E0436"),
                       QStringLiteral("str.parse_int cannot parse i32 from '%1'").arg(call.receiver.asString()),
                       call.callSpan);
        return AbelValue::makeUnknown();
    }
    return AbelValue::makeInt(static_cast<qint64>(value), TypeKind::I32);
}

AbelValue stringParseLong(BuiltinMethodCall& call)
{
    bool ok = false;
    const qlonglong value = call.receiver.asString().trimmed().toLongLong(&ok, 10);
    if (!ok) {
        call.ctx.error(QStringLiteral("E0436"),
                       QStringLiteral("str.parse_long cannot parse i64 from '%1'").arg(call.receiver.asString()),
                       call.callSpan);
        return AbelValue::makeUnknown();
    }
    return AbelValue::makeInt(static_cast<qint64>(value), TypeKind::I64);
}

AbelValue stringParseDouble(BuiltinMethodCall& call)
{
    bool ok = false;
    const double value = call.receiver.asString().trimmed().toDouble(&ok);
    if (!ok) {
        call.ctx.error(QStringLiteral("E0436"),
                       QStringLiteral("str.parse_double cannot parse f64 from '%1'").arg(call.receiver.asString()),
                       call.callSpan);
        return AbelValue::makeUnknown();
    }
    return AbelValue::makeDouble(value);
}

AbelValue stringParseBool(BuiltinMethodCall& call)
{
    const QString value = call.receiver.asString().trimmed().toLower();
    if (value == QStringLiteral("true") || value == QStringLiteral("1"))
        return AbelValue::makeBool(true);
    if (value == QStringLiteral("false") || value == QStringLiteral("0"))
        return AbelValue::makeBool(false);
    call.ctx.error(QStringLiteral("E0436"),
                   QStringLiteral("str.parse_bool cannot parse bool from '%1'").arg(call.receiver.asString()),
                   call.callSpan);
    return AbelValue::makeUnknown();
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
    registry.registerMethod({TypeKind::Vector, QStringLiteral("contains"), 1, 1, false, vectorContains, QStringLiteral("vector contains value")});
    registry.registerMethod({TypeKind::Vector, QStringLiteral("count"), 1, 1, false, vectorCount, QStringLiteral("count equal vector elements")});
    registry.registerMethod({TypeKind::Vector, QStringLiteral("extend"), 1, 1, true, vectorExtend, QStringLiteral("append vector elements")});
    registry.registerMethod({TypeKind::Vector, QStringLiteral("slice"), 2, 2, false, vectorSlice, QStringLiteral("copy vector slice")});
    registry.registerMethod({TypeKind::Vector, QStringLiteral("sort"), 0, 0, true, vectorSort, QStringLiteral("vector sort orderable elements")});
    registry.registerMethod({TypeKind::Vector, QStringLiteral("reverse"), 0, 0, true, vectorReverse, QStringLiteral("vector reverse in place")});
    registry.registerMethod({TypeKind::Vector, QStringLiteral("unique"), 0, 0, true, vectorUnique, QStringLiteral("remove adjacent equal elements")});
    registry.registerMethod({TypeKind::Vector, QStringLiteral("binary_search"), 1, 1, false, vectorBinarySearch, QStringLiteral("binary search sorted vector")});
    registry.registerMethod({TypeKind::Vector, QStringLiteral("lower_bound"), 1, 1, false, vectorLowerBound, QStringLiteral("lower bound in sorted vector")});
    registry.registerMethod({TypeKind::Vector, QStringLiteral("upper_bound"), 1, 1, false, vectorUpperBound, QStringLiteral("upper bound in sorted vector")});

    registry.registerMethod({TypeKind::Str, QStringLiteral("len"), 0, 0, false, stringLen, QStringLiteral("string length")});
    registry.registerMethod({TypeKind::Str, QStringLiteral("empty"), 0, 0, false, stringEmpty, QStringLiteral("string empty test")});
    registry.registerMethod({TypeKind::Str, QStringLiteral("contains"), 1, 1, false, stringContains, QStringLiteral("string contains")});
    registry.registerMethod({TypeKind::Str, QStringLiteral("find"), 1, 1, false, stringFind, QStringLiteral("string find, -1 when absent")});
    registry.registerMethod({TypeKind::Str, QStringLiteral("starts_with"), 1, 1, false, stringStartsWith, QStringLiteral("string prefix test")});
    registry.registerMethod({TypeKind::Str, QStringLiteral("ends_with"), 1, 1, false, stringEndsWith, QStringLiteral("string suffix test")});
    registry.registerMethod({TypeKind::Str, QStringLiteral("substr"), 2, 2, false, stringSlice, QStringLiteral("string substring")});
    registry.registerMethod({TypeKind::Str, QStringLiteral("slice"), 2, 2, false, stringSlice, QStringLiteral("string slice alias")});
    registry.registerMethod({TypeKind::Str, QStringLiteral("replace"), 2, 2, false, stringReplace, QStringLiteral("string replace all")});
    registry.registerMethod({TypeKind::Str, QStringLiteral("trim"), 0, 0, false, stringTrim, QStringLiteral("trim surrounding whitespace")});
    registry.registerMethod({TypeKind::Str, QStringLiteral("lower"), 0, 0, false, stringLower, QStringLiteral("lowercase string")});
    registry.registerMethod({TypeKind::Str, QStringLiteral("upper"), 0, 0, false, stringUpper, QStringLiteral("uppercase string")});
    registry.registerMethod({TypeKind::Str, QStringLiteral("split"), 1, 1, false, stringSplit, QStringLiteral("split string by non-empty separator")});
    registry.registerMethod({TypeKind::Str, QStringLiteral("join"), 1, 1, false, stringJoin, QStringLiteral("join vector<str> using receiver as separator")});
    registry.registerMethod({TypeKind::Str, QStringLiteral("parse_int"), 0, 0, false, stringParseInt, QStringLiteral("parse i32 from string")});
    registry.registerMethod({TypeKind::Str, QStringLiteral("parse_long"), 0, 0, false, stringParseLong, QStringLiteral("parse i64 from string")});
    registry.registerMethod({TypeKind::Str, QStringLiteral("parse_double"), 0, 0, false, stringParseDouble, QStringLiteral("parse f64 from string")});
    registry.registerMethod({TypeKind::Str, QStringLiteral("parse_bool"), 0, 0, false, stringParseBool, QStringLiteral("parse bool from string")});

    registry.registerFunction({QStringLiteral("to_str"), 1, 1, false, builtinToStr, QStringLiteral("stringify one value")});
    registry.registerFunction({QStringLiteral("build_string"), 0, -1, true, builtinBuildString, QStringLiteral("concatenate stringified values")});
    registry.registerFunction({QStringLiteral("print"), 0, -1, true, builtinPrint, QStringLiteral("print stringified values")});
    registry.registerFunction({QStringLiteral("println"), 0, -1, true, builtinPrintln, QStringLiteral("print stringified values plus newline")});
    registry.registerFunction({QStringLiteral("scan"), 0, -1, true, builtinScan, QStringLiteral("scan whitespace tokens into pointer targets")});
    registry.registerFunction({QStringLiteral("str_to_chars"), 1, 1, false, builtinStrToChars, QStringLiteral("convert str to vector<char>")});
    registry.registerFunction({QStringLiteral("chars_to_str"), 1, 1, false, builtinCharsToStr, QStringLiteral("convert vector<char> to str")});
    registry.registerFunction({QStringLiteral("abs"), 1, 1, false, builtinMath, QStringLiteral("absolute value")});
    registry.registerFunction({QStringLiteral("sqrt"), 1, 1, false, builtinMath, QStringLiteral("square root as f64")});
    registry.registerFunction({QStringLiteral("floor"), 1, 1, false, builtinMath, QStringLiteral("floor as f64")});
    registry.registerFunction({QStringLiteral("ceil"), 1, 1, false, builtinMath, QStringLiteral("ceil as f64")});
    registry.registerFunction({QStringLiteral("round"), 1, 1, false, builtinMath, QStringLiteral("round as f64")});
    registry.registerFunction({QStringLiteral("trunc"), 1, 1, false, builtinMath, QStringLiteral("truncate as f64")});
    registry.registerFunction({QStringLiteral("sin"), 1, 1, false, builtinMath, QStringLiteral("sine as f64")});
    registry.registerFunction({QStringLiteral("cos"), 1, 1, false, builtinMath, QStringLiteral("cosine as f64")});
    registry.registerFunction({QStringLiteral("tan"), 1, 1, false, builtinMath, QStringLiteral("tangent as f64")});
    registry.registerFunction({QStringLiteral("asin"), 1, 1, false, builtinMath, QStringLiteral("arcsine as f64")});
    registry.registerFunction({QStringLiteral("acos"), 1, 1, false, builtinMath, QStringLiteral("arccosine as f64")});
    registry.registerFunction({QStringLiteral("atan"), 1, 1, false, builtinMath, QStringLiteral("arctangent as f64")});
    registry.registerFunction({QStringLiteral("atan2"), 2, 2, false, builtinMath, QStringLiteral("two-argument arctangent as f64")});
    registry.registerFunction({QStringLiteral("exp"), 1, 1, false, builtinMath, QStringLiteral("exponential as f64")});
    registry.registerFunction({QStringLiteral("log"), 1, 1, false, builtinMath, QStringLiteral("natural logarithm as f64")});
    registry.registerFunction({QStringLiteral("log10"), 1, 1, false, builtinMath, QStringLiteral("base-10 logarithm as f64")});
    registry.registerFunction({QStringLiteral("pow"), 2, 2, false, builtinMath, QStringLiteral("power as f64")});
    registry.registerFunction({QStringLiteral("gcd"), 2, 2, false, builtinMath, QStringLiteral("greatest common divisor")});
    registry.registerFunction({QStringLiteral("lcm"), 2, 2, false, builtinMath, QStringLiteral("least common multiple")});
    registry.registerFunction({QStringLiteral("min"), 2, 2, false, builtinMath, QStringLiteral("minimum numeric value")});
    registry.registerFunction({QStringLiteral("max"), 2, 2, false, builtinMath, QStringLiteral("maximum numeric value")});
    registry.registerFunction({QStringLiteral("clamp"), 3, 3, false, builtinMath, QStringLiteral("clamp numeric value")});
    registry.registerFunction({QStringLiteral("debug_break"), 0, 0, false, builtinDebugBreak, QStringLiteral("emit a debug breakpoint diagnostic")});
    registry.registerFunction({QStringLiteral("debug_assert"), 1, -1, true, builtinDebugAssert, QStringLiteral("emit a diagnostic when condition is false")});
    registry.registerFunction({QStringLiteral("test_assert"), 1, -1, true, builtinTestAssert, QStringLiteral("fail current test when condition is false")});
    registry.registerFunction({QStringLiteral("test_eq"), 2, -1, true, builtinTestEq, QStringLiteral("fail current test when values are not equal")});
    registry.registerFunction({QStringLiteral("test_ne"), 2, -1, true, builtinTestNe, QStringLiteral("fail current test when values are equal")});
    registry.registerFunction({QStringLiteral("test_close"), 3, -1, true, builtinTestClose, QStringLiteral("fail current test when numeric values differ beyond eps")});

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
