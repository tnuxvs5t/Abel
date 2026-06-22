#pragma once

#include "abelcore/backend_interface.h"

#include <QList>

#include <algorithm>
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
        describeParams<typename Traits::ArgTuple>(out.params);
        return out;
    }

    template <typename Fn>
    static Runtime bind(Fn fn)
    {
        using Traits = FunctionTraits<std::decay_t<Fn>>;
        return [fn = std::move(fn)](const QList<AbelValue>& args, AbelRuntimeContext& ctx) mutable {
            if (args.size() != static_cast<qsizetype>(Traits::Arity)) {
                ctx.error(QStringLiteral("E0620"),
                          QStringLiteral("backend native function expected %1 argument(s), got %2")
                              .arg(Traits::Arity)
                              .arg(args.size()),
                          {});
                return AbelValue::makeUnknown();
            }
            return invoke<Fn, typename Traits::Return, typename Traits::ArgTuple>(fn, args, ctx);
        };
    }

private:
    template <typename T>
    struct FunctionTraits : FunctionTraits<decltype(&T::operator())> {};

    template <typename R, typename... Args>
    struct FunctionTraits<R (*)(Args...)> {
        using Return = R;
        using ArgTuple = std::tuple<Args...>;
        static constexpr size_t Arity = sizeof...(Args);
    };

    template <typename R, typename... Args>
    struct FunctionTraits<std::function<R(Args...)>> {
        using Return = R;
        using ArgTuple = std::tuple<Args...>;
        static constexpr size_t Arity = sizeof...(Args);
    };

    template <typename C, typename R, typename... Args>
    struct FunctionTraits<R (C::*)(Args...) const> {
        using Return = R;
        using ArgTuple = std::tuple<Args...>;
        static constexpr size_t Arity = sizeof...(Args);
    };

    template <typename C, typename R, typename... Args>
    struct FunctionTraits<R (C::*)(Args...)> {
        using Return = R;
        using ArgTuple = std::tuple<Args...>;
        static constexpr size_t Arity = sizeof...(Args);
    };

    template <typename>
    static constexpr bool alwaysFalse = false;

    template <typename T>
    static AbelType nativeType()
    {
        using Bare = std::remove_cvref_t<T>;
        if constexpr (std::is_same_v<Bare, void>) {
            return makeType(TypeKind::Void);
        } else if constexpr (std::is_same_v<Bare, bool>) {
            return makeType(TypeKind::Bool);
        } else if constexpr (std::is_same_v<Bare, int>) {
            return makeType(TypeKind::I32);
        } else if constexpr (std::is_same_v<Bare, qint64>) {
            return makeType(TypeKind::I64);
        } else if constexpr (std::is_same_v<Bare, double>) {
            return makeType(TypeKind::F64);
        } else if constexpr (std::is_same_v<Bare, QString>) {
            return makeType(TypeKind::Str);
        } else if constexpr (std::is_same_v<Bare, AbelValue>) {
            return makeType(TypeKind::Any);
        } else if constexpr (std::is_same_v<Bare, std::vector<int>>) {
            AbelType vectorType = makeVectorType(makeType(TypeKind::I32));
            if constexpr (std::is_lvalue_reference_v<T>)
                return makeReferenceType(vectorType);
            return vectorType;
        } else {
            static_assert(alwaysFalse<T>, "unsupported backend native type");
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
    struct NativeArg {
        using Bare = std::remove_cvref_t<T>;

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
            } else if constexpr (std::is_same_v<Bare, QString>) {
                if (value.type().kind != TypeKind::Str) {
                    ctx.error(QStringLiteral("E0621"), QStringLiteral("backend argument %1 expects str").arg(index), {});
                    return;
                }
                stringValue = value.asString();
            } else if constexpr (std::is_same_v<Bare, AbelValue>) {
                valueValue = value;
            } else if constexpr (std::is_same_v<Bare, std::vector<int>>) {
                if (value.type().kind != TypeKind::Vector || !value.type().pointee || value.type().pointee->kind != TypeKind::I32) {
                    ctx.error(QStringLiteral("E0621"), QStringLiteral("backend argument %1 expects vector<int>").arg(index), {});
                    return;
                }
                vectorValue.reserve(value.asVector()->elements.size());
                for (const auto& element : value.asVector()->elements)
                    vectorValue.push_back(static_cast<int>(element.asInt()));
                sourceVector = value.asVector();
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
            } else if constexpr (std::is_same_v<Bare, std::vector<int>>) {
                return static_cast<T>(vectorValue);
            }
        }

        void commit()
        {
            if constexpr (std::is_same_v<Bare, std::vector<int>> && !std::is_const_v<std::remove_reference_t<T>>) {
                if (!sourceVector)
                    return;
                sourceVector->elements.clear();
                sourceVector->elements.reserve(vectorValue.size());
                for (int value : vectorValue)
                    sourceVector->elements.push_back(AbelValue::makeInt(value, TypeKind::I32));
            }
        }

        bool boolValue = false;
        int intValue = 0;
        qint64 i64Value = 0;
        double doubleValue = 0.0;
        QString stringValue;
        AbelValue valueValue = AbelValue::makeUnknown();
        std::vector<int> vectorValue;
        AbelValue::VectorPtr sourceVector;
    };

    template <typename R>
    static AbelValue toAbelValue(R&& value)
    {
        using Bare = std::remove_cvref_t<R>;
        if constexpr (std::is_same_v<Bare, AbelValue>) {
            return std::forward<R>(value);
        } else if constexpr (std::is_same_v<Bare, bool>) {
            return AbelValue::makeBool(value);
        } else if constexpr (std::is_same_v<Bare, int>) {
            return AbelValue::makeInt(value, TypeKind::I32);
        } else if constexpr (std::is_same_v<Bare, qint64>) {
            return AbelValue::makeInt(value, TypeKind::I64);
        } else if constexpr (std::is_same_v<Bare, double>) {
            return AbelValue::makeDouble(value);
        } else if constexpr (std::is_same_v<Bare, QString>) {
            return AbelValue::makeString(value);
        } else {
            static_assert(alwaysFalse<R>, "unsupported backend return type");
        }
    }

    template <typename Fn, typename R, typename Tuple, size_t... I>
    static AbelValue invokeImpl(Fn& fn, const QList<AbelValue>& args, AbelRuntimeContext& ctx, std::index_sequence<I...>)
    {
        std::tuple<NativeArg<std::tuple_element_t<I, Tuple>>...> native{NativeArg<std::tuple_element_t<I, Tuple>>(args[static_cast<qsizetype>(I)], ctx, static_cast<int>(I))...};
        if (ctx.hasError())
            return AbelValue::makeUnknown();
        if constexpr (std::is_void_v<R>) {
            std::invoke(fn, std::get<I>(native).get()...);
            (std::get<I>(native).commit(), ...);
            return AbelValue::makeVoid();
        } else {
            auto result = std::invoke(fn, std::get<I>(native).get()...);
            (std::get<I>(native).commit(), ...);
            return toAbelValue(std::move(result));
        }
    }

    template <typename Fn, typename R, typename Tuple>
    static AbelValue invoke(Fn& fn, const QList<AbelValue>& args, AbelRuntimeContext& ctx)
    {
        return invokeImpl<Fn, R, Tuple>(fn, args, ctx, std::make_index_sequence<std::tuple_size_v<Tuple>>{});
    }
};

} // namespace abel
