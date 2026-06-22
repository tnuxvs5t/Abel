#include "abelcore/builtin_registry.h"

#include <QtTest/QtTest>

class AbelBuiltinRegistryTests final : public QObject {
    Q_OBJECT

private slots:
    void defaultRegistryContainsVectorMethods()
    {
        auto registry = abel::BuiltinRegistry::makeDefault();
        auto vectorType = abel::makeVectorType(abel::makeType(abel::TypeKind::I32));

        QVERIFY(registry.hasMethod(vectorType, QStringLiteral("len")));
        QVERIFY(registry.hasMethod(vectorType, QStringLiteral("empty")));
        QVERIFY(registry.hasMethod(vectorType, QStringLiteral("push")));
        QVERIFY(registry.hasMethod(vectorType, QStringLiteral("pop")));
        QVERIFY(registry.hasMethod(vectorType, QStringLiteral("front")));
        QVERIFY(registry.hasMethod(vectorType, QStringLiteral("back")));
    }

    void vectorPushAndLenRunThroughRegistry()
    {
        auto registry = abel::BuiltinRegistry::makeDefault();
        abel::AbelRuntimeContext ctx;
        auto vector = abel::AbelValue::makeVector(abel::makeType(abel::TypeKind::I32), {});
        auto* loc = ctx.createStorage(vector);

        abel::BuiltinMethodCall push{
            ctx,
            loc,
            loc->read(),
            QStringLiteral("push"),
            {abel::AbelValue::makeInt(7)},
            {},
            {},
        };
        auto pushed = registry.callMethod(std::move(push));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(pushed.type().kind, abel::TypeKind::Void);

        abel::BuiltinMethodCall len{
            ctx,
            loc,
            loc->read(),
            QStringLiteral("len"),
            {},
            {},
            {},
        };
        auto length = registry.callMethod(std::move(len));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(length.asInt(), 1);
    }

    void customFunctionCanBeRegistered()
    {
        auto registry = abel::BuiltinRegistry::makeDefault();
        registry.registerFunction({
            QStringLiteral("answer"),
            0,
            0,
            false,
            [](abel::BuiltinFunctionCall&) {
                return abel::AbelValue::makeInt(42);
            },
            QStringLiteral("test function"),
        });

        abel::AbelRuntimeContext ctx;
        abel::BuiltinFunctionCall call{ctx, QStringLiteral("answer"), {}, {}, {}};
        auto value = registry.callFunction(std::move(call));

        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(value.asInt(), 42);
    }

    void defaultStringBuiltinsWork()
    {
        auto registry = abel::BuiltinRegistry::makeDefault();
        QVERIFY(registry.hasFunction(QStringLiteral("to_str")));
        QVERIFY(registry.hasFunction(QStringLiteral("build_string")));
        QVERIFY(registry.hasFunction(QStringLiteral("print")));
        QVERIFY(registry.hasFunction(QStringLiteral("println")));

        abel::AbelRuntimeContext ctx;
        abel::BuiltinFunctionCall call{
            ctx,
            QStringLiteral("build_string"),
            {
                abel::AbelValue::makeString(QStringLiteral("x=")),
                abel::AbelValue::makeInt(7),
                abel::AbelValue::makeString(QStringLiteral(", ok=")),
                abel::AbelValue::makeBool(true),
            },
            {},
            {},
        };
        auto value = registry.callFunction(std::move(call));

        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(value.asString(), QStringLiteral("x=7, ok=true"));
    }
};

QTEST_MAIN(AbelBuiltinRegistryTests)

#include "test_builtin_registry.moc"
