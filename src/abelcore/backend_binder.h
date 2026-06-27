#pragma once

#include "abelcore/backend_interface.h"

#include <QChar>
#include <QList>
#include <QtGlobal>

#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace abel {

class AbelVariadicArgs {
public:
    AbelVariadicArgs() = default;
    explicit AbelVariadicArgs(QList<AbelValue> values)
        : m_values(std::move(values))
    {
    }

    qsizetype size() const { return m_values.size(); }
    bool empty() const { return m_values.isEmpty(); }
    const QList<AbelValue>& rawValues() const { return m_values; }

    const AbelValue& raw(qsizetype index) const
    {
        return m_values.at(index);
    }

    AbelValue value(qsizetype index) const
    {
        return unboxAny(m_values.at(index));
    }

    std::vector<AbelValue> values() const
    {
        std::vector<AbelValue> out;
        out.reserve(static_cast<size_t>(m_values.size()));
        for (const AbelValue& value : m_values)
            out.push_back(unboxAny(value));
        return out;
    }

    QString buildString() const
    {
        QString out;
        for (qsizetype i = 0; i < m_values.size(); ++i)
            out += value(i).debugString();
        return out;
    }

private:
    static AbelValue unboxAny(const AbelValue& value)
    {
        if (value.type().kind == TypeKind::Any)
            return value.asAny()->value;
        return value;
    }

    QList<AbelValue> m_values;
};

class AbelCallable {
public:
    AbelCallable() = default;
    explicit AbelCallable(AbelValue value)
        : m_value(std::move(value))
    {
    }

    bool valid() const
    {
        auto function = callableFunction();
        return function && function->invoke;
    }

    const AbelType& type() const
    {
        return callableValue().type();
    }

    AbelValue value() const
    {
        return callableValue();
    }

    AbelValue call(const std::vector<AbelValue>& args, AbelRuntimeContext& ctx, const SourceSpan& span = {}) const
    {
        auto function = callableFunction();
        if (!function || !function->invoke) {
            ctx.error(QStringLiteral("E0625"), QStringLiteral("invalid Abel callable"), span);
            return AbelValue::makeUnknown();
        }
        return function->invoke(args, ctx, span);
    }

private:
    const AbelValue& callableValue() const
    {
        if (m_value.type().kind == TypeKind::Any)
            return m_value.asAny()->value;
        return m_value;
    }

    AbelValue::FunctionPtr callableFunction() const
    {
        const AbelValue& value = callableValue();
        if (value.type().kind != TypeKind::Function)
            return {};
        return value.asFunction();
    }

    AbelValue m_value = AbelValue::makeUnknown();
};

class AbelBackendBinder {
public:
    using Runtime = std::function<AbelValue(QList<AbelValue>&, AbelRuntimeContext&)>;

    template <typename Fn>
    static AbelBackendFunction describe(const QString& symbol, Fn&&)
    {
        using Traits = FunctionTraits<std::decay_t<Fn>>;
        AbelBackendFunction out;
        out.symbol = symbol;
        out.returnType = nativeType<typename Traits::Return>();
        describeParams<typename Traits::AbelArgTuple>(out.params);
        return out;
    }

    template <typename Fn>
    static Runtime bind(Fn fn)
    {
        using Traits = FunctionTraits<std::decay_t<Fn>>;
        return [fn = std::move(fn)](QList<AbelValue>& args, AbelRuntimeContext& ctx) mutable {
            if (args.size() != static_cast<qsizetype>(Traits::AbelArity)) {
                ctx.error(QStringLiteral("E0620"),
                          QStringLiteral("backend native function expected %1 argument(s), got %2")
                              .arg(Traits::AbelArity)
                              .arg(args.size()),
                          {});
                return AbelValue::makeUnknown();
            }
            return invoke<Fn,
                          typename Traits::Return,
                          typename Traits::AbelArgTuple,
                          Traits::TakesRuntimeContext>(fn, args, ctx);
        };
    }

    template <typename Fn>
    static AbelBackendFunction describeVariadic(const QString& symbol, Fn&&)
    {
        using Traits = FunctionTraits<std::decay_t<Fn>>;
        static_assert(Traits::AbelArity == 1,
                      "variadic backend function must take exactly one payload argument, plus optional AbelRuntimeContext&");
        using Payload = std::tuple_element_t<0, typename Traits::AbelArgTuple>;
        static_assert(IsVariadicPayloadArg<Payload>::value,
                      "variadic backend payload must be AbelVariadicArgs or std::vector<AbelValue>");

        AbelBackendFunction out;
        out.symbol = symbol;
        out.returnType = nativeType<typename Traits::Return>();
        out.params.push_back(makeType(TypeKind::Any));
        out.variadic = true;
        return out;
    }

    template <typename Fn>
    static Runtime bindVariadic(Fn fn)
    {
        using Traits = FunctionTraits<std::decay_t<Fn>>;
        static_assert(Traits::AbelArity == 1,
                      "variadic backend function must take exactly one payload argument, plus optional AbelRuntimeContext&");
        using Payload = std::tuple_element_t<0, typename Traits::AbelArgTuple>;
        static_assert(IsVariadicPayloadArg<Payload>::value,
                      "variadic backend payload must be AbelVariadicArgs or std::vector<AbelValue>");

        return [fn = std::move(fn)](const QList<AbelValue>& args, AbelRuntimeContext& ctx) mutable {
            return invokeVariadic<Fn,
                                  typename Traits::Return,
                                  Payload,
                                  Traits::TakesRuntimeContext>(fn, args, ctx);
        };
    }

private:
    template <typename>
    static constexpr bool alwaysFalse = false;

    template <typename T>
    struct IsRuntimeContextArg
        : std::bool_constant<std::is_lvalue_reference_v<T>
                             && std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, AbelRuntimeContext>> {};

    template <typename T>
    struct IsStdVector : std::false_type {};

    template <typename T, typename Alloc>
    struct IsStdVector<std::vector<T, Alloc>> : std::true_type {
        using Element = T;
    };

    template <typename T>
    struct IsStdVectorOfAbelValue : std::false_type {};

    template <typename Alloc>
    struct IsStdVectorOfAbelValue<std::vector<AbelValue, Alloc>> : std::true_type {};

    template <typename T>
    struct IsVariadicPayloadArg {
        using Bare = std::remove_cvref_t<T>;
        static constexpr bool value = std::is_same_v<Bare, AbelVariadicArgs>
            || IsStdVectorOfAbelValue<Bare>::value;
    };

    template <typename Tuple, typename Indexes>
    struct TupleTakeImpl;

    template <typename Tuple, size_t... I>
    struct TupleTakeImpl<Tuple, std::index_sequence<I...>> {
        using Type = std::tuple<std::tuple_element_t<I, Tuple>...>;
    };

    template <typename Tuple, size_t Count>
    using TupleTake = typename TupleTakeImpl<Tuple, std::make_index_sequence<Count>>::Type;

    template <typename Tuple, bool Empty = (std::tuple_size_v<Tuple> == 0)>
    struct LastArgIsRuntimeContext : std::false_type {};

    template <typename Tuple>
    struct LastArgIsRuntimeContext<Tuple, false>
        : IsRuntimeContextArg<std::tuple_element_t<std::tuple_size_v<Tuple> - 1, Tuple>> {};

    template <typename Tuple, bool HasContext>
    struct AbelArgTupleSelector {
        using Type = Tuple;
    };

    template <typename Tuple>
    struct AbelArgTupleSelector<Tuple, true> {
        using Type = TupleTake<Tuple, std::tuple_size_v<Tuple> - 1>;
    };

    template <typename Tuple>
    struct StripFinalRuntimeContextArg {
        static constexpr bool HasContext = LastArgIsRuntimeContext<Tuple>::value;
        using Type = typename AbelArgTupleSelector<Tuple, HasContext>::Type;
    };

    template <typename T>
    struct FunctionTraits : FunctionTraits<decltype(&T::operator())> {};

    template <typename R, typename... Args>
    struct FunctionTraits<R (*)(Args...)> {
        using Return = R;
        using ArgTuple = std::tuple<Args...>;
        using AbelArgTuple = typename StripFinalRuntimeContextArg<ArgTuple>::Type;
        static constexpr size_t Arity = sizeof...(Args);
        static constexpr size_t AbelArity = std::tuple_size_v<AbelArgTuple>;
        static constexpr bool TakesRuntimeContext = StripFinalRuntimeContextArg<ArgTuple>::HasContext;
    };

    template <typename R, typename... Args>
    struct FunctionTraits<std::function<R(Args...)>> {
        using Return = R;
        using ArgTuple = std::tuple<Args...>;
        using AbelArgTuple = typename StripFinalRuntimeContextArg<ArgTuple>::Type;
        static constexpr size_t Arity = sizeof...(Args);
        static constexpr size_t AbelArity = std::tuple_size_v<AbelArgTuple>;
        static constexpr bool TakesRuntimeContext = StripFinalRuntimeContextArg<ArgTuple>::HasContext;
    };

    template <typename C, typename R, typename... Args>
    struct FunctionTraits<R (C::*)(Args...) const> {
        using Return = R;
        using ArgTuple = std::tuple<Args...>;
        using AbelArgTuple = typename StripFinalRuntimeContextArg<ArgTuple>::Type;
        static constexpr size_t Arity = sizeof...(Args);
        static constexpr size_t AbelArity = std::tuple_size_v<AbelArgTuple>;
        static constexpr bool TakesRuntimeContext = StripFinalRuntimeContextArg<ArgTuple>::HasContext;
    };

    template <typename C, typename R, typename... Args>
    struct FunctionTraits<R (C::*)(Args...)> {
        using Return = R;
        using ArgTuple = std::tuple<Args...>;
        using AbelArgTuple = typename StripFinalRuntimeContextArg<ArgTuple>::Type;
        static constexpr size_t Arity = sizeof...(Args);
        static constexpr size_t AbelArity = std::tuple_size_v<AbelArgTuple>;
        static constexpr bool TakesRuntimeContext = StripFinalRuntimeContextArg<ArgTuple>::HasContext;
    };

    template <typename T>
    static AbelType nativeScalarType()
    {
        using Bare = std::remove_cvref_t<T>;
        if constexpr (std::is_same_v<Bare, bool>) {
            return makeType(TypeKind::Bool);
        } else if constexpr (std::is_same_v<Bare, qint8>) {
            return makeType(TypeKind::I8);
        } else if constexpr (std::is_same_v<Bare, qint16>) {
            return makeType(TypeKind::I16);
        } else if constexpr (std::is_same_v<Bare, int>) {
            return makeType(TypeKind::I32);
        } else if constexpr (std::is_same_v<Bare, qint64>) {
            return makeType(TypeKind::I64);
        } else if constexpr (std::is_same_v<Bare, quint8>) {
            return makeType(TypeKind::U8);
        } else if constexpr (std::is_same_v<Bare, quint16>) {
            return makeType(TypeKind::U16);
        } else if constexpr (std::is_same_v<Bare, quint32>) {
            return makeType(TypeKind::U32);
        } else if constexpr (std::is_same_v<Bare, quint64>) {
            return makeType(TypeKind::U64);
        } else if constexpr (std::is_same_v<Bare, double>) {
            return makeType(TypeKind::F64);
        } else if constexpr (std::is_same_v<Bare, QChar> || std::is_same_v<Bare, char>) {
            return makeType(TypeKind::Char);
        } else if constexpr (std::is_same_v<Bare, QString>) {
            return makeType(TypeKind::Str);
        } else if constexpr (std::is_same_v<Bare, AbelCallable>) {
            return makeType(TypeKind::Any);
        } else if constexpr (std::is_same_v<Bare, AbelValue>) {
            return makeType(TypeKind::Any);
        } else {
            static_assert(alwaysFalse<T>, "unsupported backend scalar native type");
        }
    }

    template <typename T>
    static AbelType nativeType()
    {
        using Bare = std::remove_cvref_t<T>;
        if constexpr (std::is_same_v<Bare, void>) {
            return makeType(TypeKind::Void);
        } else if constexpr (IsRuntimeContextArg<T>::value) {
            static_assert(alwaysFalse<T>, "AbelRuntimeContext& is only allowed as the last C++ backend parameter");
        } else if constexpr (IsStdVector<Bare>::value) {
            using Element = typename IsStdVector<Bare>::Element;
            AbelType vectorType = makeVectorType(nativeScalarType<Element>());
            if constexpr (std::is_const_v<std::remove_reference_t<T>>)
                vectorType = makeConstType(vectorType);
            if constexpr (std::is_lvalue_reference_v<T>)
                return makeReferenceType(vectorType);
            return vectorType;
        } else {
            AbelType scalarType = nativeScalarType<T>();
            if constexpr (std::is_const_v<std::remove_reference_t<T>>)
                scalarType = makeConstType(scalarType);
            if constexpr (std::is_lvalue_reference_v<T>)
                return makeReferenceType(scalarType);
            return scalarType;
        }
    }

    template <typename Tuple, size_t... I>
    static void describeParamsImpl(std::vector<AbelType>& out, std::index_sequence<I...>)
    {
        (out.push_back(nativeType<std::tuple_element_t<I, Tuple>>()), ...);
    }

    template <typename Tuple>
    static void describeParams(std::vector<AbelType>& out)
    {
        describeParamsImpl<Tuple>(out, std::make_index_sequence<std::tuple_size_v<Tuple>>{});
    }

    template <typename T>
    static T fromAbelScalar(const AbelValue& value)
    {
        using Bare = std::remove_cvref_t<T>;
        if constexpr (std::is_same_v<Bare, bool>) {
            return value.asBool();
        } else if constexpr (std::is_same_v<Bare, qint8>) {
            return static_cast<qint8>(value.asInt());
        } else if constexpr (std::is_same_v<Bare, qint16>) {
            return static_cast<qint16>(value.asInt());
        } else if constexpr (std::is_same_v<Bare, int>) {
            return static_cast<int>(value.asInt());
        } else if constexpr (std::is_same_v<Bare, qint64>) {
            return value.asInt();
        } else if constexpr (std::is_same_v<Bare, quint8>) {
            return static_cast<quint8>(value.asInt());
        } else if constexpr (std::is_same_v<Bare, quint16>) {
            return static_cast<quint16>(value.asInt());
        } else if constexpr (std::is_same_v<Bare, quint32>) {
            return static_cast<quint32>(value.asInt());
        } else if constexpr (std::is_same_v<Bare, quint64>) {
            return static_cast<quint64>(value.asInt());
        } else if constexpr (std::is_same_v<Bare, double>) {
            return value.asDouble();
        } else if constexpr (std::is_same_v<Bare, QChar>) {
            return value.asChar();
        } else if constexpr (std::is_same_v<Bare, char>) {
            return value.asChar().toLatin1();
        } else if constexpr (std::is_same_v<Bare, QString>) {
            return value.asString();
        } else if constexpr (std::is_same_v<Bare, AbelCallable>) {
            return AbelCallable(value);
        } else if constexpr (std::is_same_v<Bare, AbelValue>) {
            return value;
        } else {
            static_assert(alwaysFalse<T>, "unsupported backend scalar native type");
        }
    }

    template <typename T>
    static AbelValue toAbelScalar(T&& value)
    {
        using Bare = std::remove_cvref_t<T>;
        if constexpr (std::is_same_v<Bare, AbelValue>) {
            return std::forward<T>(value);
        } else if constexpr (std::is_same_v<Bare, AbelCallable>) {
            return value.value();
        } else if constexpr (std::is_same_v<Bare, bool>) {
            return AbelValue::makeBool(value);
        } else if constexpr (std::is_same_v<Bare, qint8>) {
            return AbelValue::makeInt(value, TypeKind::I8);
        } else if constexpr (std::is_same_v<Bare, qint16>) {
            return AbelValue::makeInt(value, TypeKind::I16);
        } else if constexpr (std::is_same_v<Bare, int>) {
            return AbelValue::makeInt(value, TypeKind::I32);
        } else if constexpr (std::is_same_v<Bare, qint64>) {
            return AbelValue::makeInt(value, TypeKind::I64);
        } else if constexpr (std::is_same_v<Bare, quint8>) {
            return AbelValue::makeInt(static_cast<qint64>(value), TypeKind::U8);
        } else if constexpr (std::is_same_v<Bare, quint16>) {
            return AbelValue::makeInt(static_cast<qint64>(value), TypeKind::U16);
        } else if constexpr (std::is_same_v<Bare, quint32>) {
            return AbelValue::makeInt(static_cast<qint64>(value), TypeKind::U32);
        } else if constexpr (std::is_same_v<Bare, quint64>) {
            return AbelValue::makeInt(static_cast<qint64>(value), TypeKind::U64);
        } else if constexpr (std::is_same_v<Bare, double>) {
            return AbelValue::makeDouble(value);
        } else if constexpr (std::is_same_v<Bare, QChar>) {
            return AbelValue::makeChar(value);
        } else if constexpr (std::is_same_v<Bare, char>) {
            return AbelValue::makeChar(QChar::fromLatin1(value));
        } else if constexpr (std::is_same_v<Bare, QString>) {
            return AbelValue::makeString(value);
        } else {
            static_assert(alwaysFalse<T>, "unsupported backend scalar return type");
        }
    }

    template <typename T>
    struct NativeArg {
        using Bare = std::remove_cvref_t<T>;
        using VectorStorage = std::conditional_t<IsStdVector<Bare>::value, Bare, std::vector<int>>;

        explicit NativeArg(const AbelValue& value, AbelRuntimeContext& ctx, int index)
        {
            if constexpr (std::is_same_v<Bare, bool>) {
                if (value.type().kind != TypeKind::Bool) {
                    ctx.error(QStringLiteral("E0621"), QStringLiteral("backend argument %1 expects bool").arg(index), {});
                    return;
                }
                boolValue = value.asBool();
            } else if constexpr (std::is_same_v<Bare, qint8>) {
                if (!value.type().isInteger()) {
                    ctx.error(QStringLiteral("E0621"), QStringLiteral("backend argument %1 expects i8").arg(index), {});
                    return;
                }
                i8Value = static_cast<qint8>(value.asInt());
            } else if constexpr (std::is_same_v<Bare, qint16>) {
                if (!value.type().isInteger()) {
                    ctx.error(QStringLiteral("E0621"), QStringLiteral("backend argument %1 expects i16").arg(index), {});
                    return;
                }
                i16Value = static_cast<qint16>(value.asInt());
            } else if constexpr (std::is_same_v<Bare, int>) {
                if (!value.type().isInteger()) {
                    ctx.error(QStringLiteral("E0621"), QStringLiteral("backend argument %1 expects int").arg(index), {});
                    return;
                }
                intValue = static_cast<int>(value.asInt());
            } else if constexpr (std::is_same_v<Bare, qint64>) {
                if (!value.type().isInteger()) {
                    ctx.error(QStringLiteral("E0621"), QStringLiteral("backend argument %1 expects i64").arg(index), {});
                    return;
                }
                i64Value = value.asInt();
            } else if constexpr (std::is_same_v<Bare, quint8>) {
                if (!value.type().isInteger()) {
                    ctx.error(QStringLiteral("E0621"), QStringLiteral("backend argument %1 expects u8").arg(index), {});
                    return;
                }
                u8Value = static_cast<quint8>(value.asInt());
            } else if constexpr (std::is_same_v<Bare, quint16>) {
                if (!value.type().isInteger()) {
                    ctx.error(QStringLiteral("E0621"), QStringLiteral("backend argument %1 expects u16").arg(index), {});
                    return;
                }
                u16Value = static_cast<quint16>(value.asInt());
            } else if constexpr (std::is_same_v<Bare, quint32>) {
                if (!value.type().isInteger()) {
                    ctx.error(QStringLiteral("E0621"), QStringLiteral("backend argument %1 expects u32").arg(index), {});
                    return;
                }
                u32Value = static_cast<quint32>(value.asInt());
            } else if constexpr (std::is_same_v<Bare, quint64>) {
                if (!value.type().isInteger()) {
                    ctx.error(QStringLiteral("E0621"), QStringLiteral("backend argument %1 expects u64").arg(index), {});
                    return;
                }
                u64Value = static_cast<quint64>(value.asInt());
            } else if constexpr (std::is_same_v<Bare, double>) {
                if (!value.type().isNumeric()) {
                    ctx.error(QStringLiteral("E0621"), QStringLiteral("backend argument %1 expects f64").arg(index), {});
                    return;
                }
                doubleValue = value.asDouble();
            } else if constexpr (std::is_same_v<Bare, QChar>) {
                if (value.type().kind != TypeKind::Char) {
                    ctx.error(QStringLiteral("E0621"), QStringLiteral("backend argument %1 expects char").arg(index), {});
                    return;
                }
                charValue = value.asChar();
            } else if constexpr (std::is_same_v<Bare, char>) {
                if (value.type().kind != TypeKind::Char) {
                    ctx.error(QStringLiteral("E0621"), QStringLiteral("backend argument %1 expects char").arg(index), {});
                    return;
                }
                char8Value = value.asChar().toLatin1();
            } else if constexpr (std::is_same_v<Bare, QString>) {
                if (value.type().kind != TypeKind::Str) {
                    ctx.error(QStringLiteral("E0621"), QStringLiteral("backend argument %1 expects str").arg(index), {});
                    return;
                }
                stringValue = value.asString();
            } else if constexpr (std::is_same_v<Bare, AbelValue>) {
                valueValue = value;
            } else if constexpr (std::is_same_v<Bare, AbelCallable>) {
                callableValue = AbelCallable(value);
                if (!callableValue.valid()) {
                    ctx.error(QStringLiteral("E0621"),
                              QStringLiteral("backend argument %1 expects callable").arg(index),
                              {});
                    return;
                }
            } else if constexpr (IsStdVector<Bare>::value) {
                using Element = typename IsStdVector<Bare>::Element;
                const AbelType elementType = nativeScalarType<Element>();
                if (value.type().kind != TypeKind::Vector || !value.type().pointee || *value.type().pointee != elementType) {
                    ctx.error(QStringLiteral("E0621"),
                              QStringLiteral("backend argument %1 expects %2")
                                  .arg(index)
                                  .arg(nativeType<Bare>().displayName()),
                              {});
                    return;
                }
                auto vector = value.asVector();
                vectorValue.reserve(vector->elements.size());
                for (const auto& element : vector->elements)
                    vectorValue.push_back(fromAbelScalar<Element>(element));
                sourceVector = std::move(vector);
            } else {
                static_assert(alwaysFalse<T>, "unsupported backend native argument type");
            }
        }

        decltype(auto) get()
        {
            if constexpr (std::is_same_v<Bare, bool>) {
                return static_cast<T>(boolValue);
            } else if constexpr (std::is_same_v<Bare, qint8>) {
                return static_cast<T>(i8Value);
            } else if constexpr (std::is_same_v<Bare, qint16>) {
                return static_cast<T>(i16Value);
            } else if constexpr (std::is_same_v<Bare, int>) {
                return static_cast<T>(intValue);
            } else if constexpr (std::is_same_v<Bare, qint64>) {
                return static_cast<T>(i64Value);
            } else if constexpr (std::is_same_v<Bare, quint8>) {
                return static_cast<T>(u8Value);
            } else if constexpr (std::is_same_v<Bare, quint16>) {
                return static_cast<T>(u16Value);
            } else if constexpr (std::is_same_v<Bare, quint32>) {
                return static_cast<T>(u32Value);
            } else if constexpr (std::is_same_v<Bare, quint64>) {
                return static_cast<T>(u64Value);
            } else if constexpr (std::is_same_v<Bare, double>) {
                return static_cast<T>(doubleValue);
            } else if constexpr (std::is_same_v<Bare, QChar>) {
                if constexpr (std::is_lvalue_reference_v<T>)
                    return static_cast<T>(charValue);
                else
                    return charValue;
            } else if constexpr (std::is_same_v<Bare, char>) {
                return static_cast<T>(char8Value);
            } else if constexpr (std::is_same_v<Bare, QString>) {
                if constexpr (std::is_lvalue_reference_v<T>)
                    return static_cast<T>(stringValue);
                else
                    return stringValue;
            } else if constexpr (std::is_same_v<Bare, AbelValue>) {
                if constexpr (std::is_lvalue_reference_v<T>)
                    return static_cast<T>(valueValue);
                else
                    return valueValue;
            } else if constexpr (std::is_same_v<Bare, AbelCallable>) {
                if constexpr (std::is_lvalue_reference_v<T>)
                    return static_cast<T>(callableValue);
                else
                    return callableValue;
            } else if constexpr (IsStdVector<Bare>::value) {
                return static_cast<T>(vectorValue);
            } else {
                static_assert(alwaysFalse<T>, "unsupported backend native argument type");
            }
        }

        void commit(AbelValue& target)
        {
            if constexpr (std::is_lvalue_reference_v<T>
                          && !std::is_const_v<std::remove_reference_t<T>>) {
                if constexpr (IsStdVector<Bare>::value) {
                    using Element = typename IsStdVector<Bare>::Element;
                    std::vector<AbelValue> elements;
                    elements.reserve(vectorValue.size());
                    for (size_t i = 0; i < vectorValue.size(); ++i) {
                        Element element = vectorValue[i];
                        elements.push_back(toAbelScalar(element));
                    }
                    target = AbelValue::makeVector(nativeScalarType<Element>(), std::move(elements));
                    if (sourceVector) {
                        sourceVector->elements = target.asVector()->elements;
                    }
                } else {
                    target = toAbelScalar(Bare(get()));
                }
            }
        }

        bool boolValue = false;
        qint8 i8Value = 0;
        qint16 i16Value = 0;
        int intValue = 0;
        qint64 i64Value = 0;
        quint8 u8Value = 0;
        quint16 u16Value = 0;
        quint32 u32Value = 0;
        quint64 u64Value = 0;
        double doubleValue = 0.0;
        QChar charValue;
        char char8Value = '\0';
        QString stringValue;
        AbelValue valueValue = AbelValue::makeUnknown();
        AbelCallable callableValue;
        VectorStorage vectorValue;
        AbelValue::VectorPtr sourceVector;
    };

    template <typename R>
    static AbelValue toAbelValue(R&& value)
    {
        using Bare = std::remove_cvref_t<R>;
        if constexpr (IsStdVector<Bare>::value) {
            using Element = typename IsStdVector<Bare>::Element;
            std::vector<AbelValue> elements;
            elements.reserve(value.size());
            for (size_t i = 0; i < value.size(); ++i) {
                Element element = value[i];
                elements.push_back(toAbelScalar(element));
            }
            return AbelValue::makeVector(nativeScalarType<Element>(), std::move(elements));
        } else {
            return toAbelScalar(std::forward<R>(value));
        }
    }

    template <typename Fn, typename R, typename AbelTuple, bool TakesRuntimeContext, size_t... I>
    static AbelValue invokeImpl(Fn& fn,
                                QList<AbelValue>& args,
                                AbelRuntimeContext& ctx,
                                std::index_sequence<I...>)
    {
        std::tuple<NativeArg<std::tuple_element_t<I, AbelTuple>>...> native{
            NativeArg<std::tuple_element_t<I, AbelTuple>>(args[static_cast<qsizetype>(I)], ctx, static_cast<int>(I))...
        };
        if (ctx.hasError())
            return AbelValue::makeUnknown();

        if constexpr (std::is_void_v<R>) {
            if constexpr (TakesRuntimeContext)
                std::invoke(fn, std::get<I>(native).get()..., ctx);
            else
                std::invoke(fn, std::get<I>(native).get()...);
            (std::get<I>(native).commit(args[static_cast<qsizetype>(I)]), ...);
            if (ctx.hasError())
                return AbelValue::makeUnknown();
            return AbelValue::makeVoid();
        } else {
            auto result = [&]() {
                if constexpr (TakesRuntimeContext)
                    return std::invoke(fn, std::get<I>(native).get()..., ctx);
                else
                    return std::invoke(fn, std::get<I>(native).get()...);
            }();
            (std::get<I>(native).commit(args[static_cast<qsizetype>(I)]), ...);
            if (ctx.hasError())
                return AbelValue::makeUnknown();
            return toAbelValue(std::move(result));
        }
    }

    template <typename Fn, typename R, typename AbelTuple, bool TakesRuntimeContext>
    static AbelValue invoke(Fn& fn, QList<AbelValue>& args, AbelRuntimeContext& ctx)
    {
        return invokeImpl<Fn, R, AbelTuple, TakesRuntimeContext>(
            fn,
            args,
            ctx,
            std::make_index_sequence<std::tuple_size_v<AbelTuple>>{});
    }

    static std::vector<AbelValue> unboxedVector(const AbelVariadicArgs& args)
    {
        return args.values();
    }

    template <typename Payload>
    struct VariadicPayloadStorage {
        using Bare = std::remove_cvref_t<Payload>;

        explicit VariadicPayloadStorage(const QList<AbelValue>& args)
            : value(make(args))
        {
        }

        decltype(auto) get()
        {
            if constexpr (std::is_lvalue_reference_v<Payload>)
                return static_cast<Payload>(value);
            else
                return std::move(value);
        }

        static Bare make(const QList<AbelValue>& args)
        {
            if constexpr (std::is_same_v<Bare, AbelVariadicArgs>) {
                return AbelVariadicArgs(args);
            } else if constexpr (IsStdVectorOfAbelValue<Bare>::value) {
                return unboxedVector(AbelVariadicArgs(args));
            } else {
                static_assert(alwaysFalse<Payload>, "unsupported variadic backend payload type");
            }
        }

        Bare value;
    };

    template <typename Fn, typename R, typename Payload, bool TakesRuntimeContext>
    static AbelValue invokeVariadic(Fn& fn, const QList<AbelValue>& args, AbelRuntimeContext& ctx)
    {
        VariadicPayloadStorage<Payload> payload(args);
        if constexpr (std::is_void_v<R>) {
            if constexpr (TakesRuntimeContext)
                std::invoke(fn, payload.get(), ctx);
            else
                std::invoke(fn, payload.get());
            if (ctx.hasError())
                return AbelValue::makeUnknown();
            return AbelValue::makeVoid();
        } else {
            auto result = [&]() {
                if constexpr (TakesRuntimeContext)
                    return std::invoke(fn, payload.get(), ctx);
                else
                    return std::invoke(fn, payload.get());
            }();
            if (ctx.hasError())
                return AbelValue::makeUnknown();
            return toAbelValue(std::move(result));
        }
    }
};

} // namespace abel
