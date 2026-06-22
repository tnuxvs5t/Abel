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
        QVERIFY(registry.hasMethod(vectorType, QStringLiteral("clear")));
        QVERIFY(registry.hasMethod(vectorType, QStringLiteral("reserve")));
        QVERIFY(registry.hasMethod(vectorType, QStringLiteral("resize")));
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

    void vectorResizeAndClearRunThroughRegistry()
    {
        auto registry = abel::BuiltinRegistry::makeDefault();
        abel::AbelRuntimeContext ctx;
        auto vector = abel::AbelValue::makeVector(abel::makeType(abel::TypeKind::I32), {abel::AbelValue::makeInt(4)});
        auto* loc = ctx.createStorage(vector);

        abel::BuiltinMethodCall resize{
            ctx,
            loc,
            loc->read(),
            QStringLiteral("resize"),
            {abel::AbelValue::makeInt(3)},
            {},
            {},
        };
        auto resized = registry.callMethod(std::move(resize));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(resized.type().kind, abel::TypeKind::Void);
        QCOMPARE(loc->read().asVector()->elements.size(), static_cast<size_t>(3));
        QCOMPARE(loc->read().asVector()->elements[0].asInt(), 4);
        QCOMPARE(loc->read().asVector()->elements[2].asInt(), 0);

        abel::BuiltinMethodCall clear{
            ctx,
            loc,
            loc->read(),
            QStringLiteral("clear"),
            {},
            {},
            {},
        };
        auto cleared = registry.callMethod(std::move(clear));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(cleared.type().kind, abel::TypeKind::Void);
        QCOMPARE(loc->read().asVector()->elements.size(), static_cast<size_t>(0));
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
        QVERIFY(registry.hasFunction(QStringLiteral("str_to_chars")));
        QVERIFY(registry.hasFunction(QStringLiteral("chars_to_str")));

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

    void stringCharConversionsWork()
    {
        auto registry = abel::BuiltinRegistry::makeDefault();
        abel::AbelRuntimeContext ctx;

        abel::BuiltinFunctionCall toChars{
            ctx,
            QStringLiteral("str_to_chars"),
            {abel::AbelValue::makeString(QStringLiteral("ab"))},
            {},
            {},
        };
        auto chars = registry.callFunction(std::move(toChars));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(chars.type().kind, abel::TypeKind::Vector);
        QVERIFY(chars.type().pointee);
        QCOMPARE(chars.type().pointee->kind, abel::TypeKind::Char);
        QCOMPARE(chars.asVector()->elements.size(), static_cast<size_t>(2));
        QCOMPARE(chars.asVector()->elements[0].asChar(), QChar('a'));
        QCOMPARE(chars.asVector()->elements[1].asChar(), QChar('b'));

        abel::BuiltinFunctionCall toStr{
            ctx,
            QStringLiteral("chars_to_str"),
            {chars},
            {},
            {},
        };
        auto text = registry.callFunction(std::move(toStr));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(text.asString(), QStringLiteral("ab"));
    }
};

QTEST_MAIN(AbelBuiltinRegistryTests)

#include "test_builtin_registry.moc"
