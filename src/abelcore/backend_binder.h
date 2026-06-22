#pragma once

#include "abelcore/backend_interface.h"

#include <QChar>
#include <QList>

#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace abel {

class AbelBackendBinder {
public:
    using Runtime = std::function<AbelValue(const QList<AbelValue>&, AbelRuntimeContext&)>;

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
        return [fn = std::move(fn)](const QList<AbelValue>& args, AbelRuntimeContext& ctx) mutable {
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
        } else if constexpr (std::is_same_v<Bare, int>) {
            return makeType(TypeKind::I32);
        } else if constexpr (std::is_same_v<Bare, qint64>) {
            return makeType(TypeKind::I64);
        } else if constexpr (std::is_same_v<Bare, double>) {
            return makeType(TypeKind::F64);
        } else if constexpr (std::is_same_v<Bare, QChar> || std::is_same_v<Bare, char>) {
            return makeType(TypeKind::Char);
        } else if constexpr (std::is_same_v<Bare, QString>) {
            return makeType(TypeKind::Str);
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
            if constexpr (std::is_lvalue_reference_v<T>)
                return makeReferenceType(vectorType);
            return vectorType;
        } else {
            return nativeScalarType<T>();
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
        } else if constexpr (std::is_same_v<Bare, int>) {
            return static_cast<int>(value.asInt());
        } else if constexpr (std::is_same_v<Bare, qint64>) {
            return value.asInt();
        } else if constexpr (std::is_same_v<Bare, double>) {
            return value.asDouble();
        } else if constexpr (std::is_same_v<Bare, QChar>) {
            return value.asChar();
        } else if constexpr (std::is_same_v<Bare, char>) {
            return value.asChar().toLatin1();
        } else if constexpr (std::is_same_v<Bare, QString>) {
            return value.asString();
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
        } else if constexpr (std::is_same_v<Bare, bool>) {
            return AbelValue::makeBool(value);
        } else if constexpr (std::is_same_v<Bare, int>) {
            return AbelValue::makeInt(value, TypeKind::I32);
        } else if constexpr (std::is_same_v<Bare, qint64>) {
            return AbelValue::makeInt(value, TypeKind::I64);
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
            } else if constexpr (std::is_same_v<Bare, int>) {
                return static_cast<T>(intValue);
            } else if constexpr (std::is_same_v<Bare, qint64>) {
                return static_cast<T>(i64Value);
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
            } else if constexpr (IsStdVector<Bare>::value) {
                return static_cast<T>(vectorValue);
            } else {
                static_assert(alwaysFalse<T>, "unsupported backend native argument type");
            }
        }

        void commit()
        {
            if constexpr (IsStdVector<Bare>::value
                          && std::is_lvalue_reference_v<T>
                          && !std::is_const_v<std::remove_reference_t<T>>) {
                using Element = typename IsStdVector<Bare>::Element;
                if (!sourceVector)
                    return;
                sourceVector->elements.clear();
                sourceVector->elements.reserve(vectorValue.size());
                for (size_t i = 0; i < vectorValue.size(); ++i) {
                    Element element = vectorValue[i];
                    sourceVector->elements.push_back(toAbelScalar(element));
                }
            }
        }

        bool boolValue = false;
        int intValue = 0;
        qint64 i64Value = 0;
        double doubleValue = 0.0;
        QChar charValue;
        char char8Value = '\0';
        QString stringValue;
        AbelValue valueValue = AbelValue::makeUnknown();
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
                                const QList<AbelValue>& args,
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
            (std::get<I>(native).commit(), ...);
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
            (std::get<I>(native).commit(), ...);
            if (ctx.hasError())
                return AbelValue::makeUnknown();
            return toAbelValue(std::move(result));
        }
    }

    template <typename Fn, typename R, typename AbelTuple, bool TakesRuntimeContext>
    static AbelValue invoke(Fn& fn, const QList<AbelValue>& args, AbelRuntimeContext& ctx)
    {
        return invokeImpl<Fn, R, AbelTuple, TakesRuntimeContext>(
            fn,
            args,
            ctx,
            std::make_index_sequence<std::tuple_size_v<AbelTuple>>{});
    }
};

} // namespace abel
