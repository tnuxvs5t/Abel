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
        QVERIFY(registry.hasMethod(vectorType, QStringLiteral("insert")));
        QVERIFY(registry.hasMethod(vectorType, QStringLiteral("erase")));
        QVERIFY(registry.hasMethod(vectorType, QStringLiteral("find")));
        QVERIFY(registry.hasMethod(vectorType, QStringLiteral("sort")));
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

    void vectorInsertEraseFindSortRunThroughRegistry()
    {
        auto registry = abel::BuiltinRegistry::makeDefault();
        abel::AbelRuntimeContext ctx;
        auto vector = abel::AbelValue::makeVector(abel::makeType(abel::TypeKind::I32),
                                                  {abel::AbelValue::makeInt(3), abel::AbelValue::makeInt(1)});
        auto* loc = ctx.createStorage(vector);

        abel::BuiltinMethodCall insert{
            ctx,
            loc,
            loc->read(),
            QStringLiteral("insert"),
            {abel::AbelValue::makeInt(1), abel::AbelValue::makeInt(2)},
            {},
            {},
        };
        auto inserted = registry.callMethod(std::move(insert));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(inserted.type().kind, abel::TypeKind::Void);
        QCOMPARE(loc->read().asVector()->elements.size(), static_cast<size_t>(3));
        QCOMPARE(loc->read().asVector()->elements[0].asInt(), 3);
        QCOMPARE(loc->read().asVector()->elements[1].asInt(), 2);
        QCOMPARE(loc->read().asVector()->elements[2].asInt(), 1);

        abel::BuiltinMethodCall find{
            ctx,
            loc,
            loc->read(),
            QStringLiteral("find"),
            {abel::AbelValue::makeInt(2)},
            {},
            {},
        };
        auto found = registry.callMethod(std::move(find));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(found.asInt(), 1);

        abel::BuiltinMethodCall missing{
            ctx,
            loc,
            loc->read(),
            QStringLiteral("find"),
            {abel::AbelValue::makeInt(9)},
            {},
            {},
        };
        auto absent = registry.callMethod(std::move(missing));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(absent.asInt(), -1);

        abel::BuiltinMethodCall sort{
            ctx,
            loc,
            loc->read(),
            QStringLiteral("sort"),
            {},
            {},
            {},
        };
        auto sorted = registry.callMethod(std::move(sort));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(sorted.type().kind, abel::TypeKind::Void);
        QCOMPARE(loc->read().asVector()->elements[0].asInt(), 1);
        QCOMPARE(loc->read().asVector()->elements[1].asInt(), 2);
        QCOMPARE(loc->read().asVector()->elements[2].asInt(), 3);

        abel::BuiltinMethodCall erase{
            ctx,
            loc,
            loc->read(),
            QStringLiteral("erase"),
            {abel::AbelValue::makeInt(1)},
            {},
            {},
        };
        auto removed = registry.callMethod(std::move(erase));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(removed.asInt(), 2);
        QCOMPARE(loc->read().asVector()->elements.size(), static_cast<size_t>(2));
        QCOMPARE(loc->read().asVector()->elements[0].asInt(), 1);
        QCOMPARE(loc->read().asVector()->elements[1].asInt(), 3);
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
        const abel::AbelType strType = abel::makeType(abel::TypeKind::Str);
        QVERIFY(registry.hasFunction(QStringLiteral("to_str")));
        QVERIFY(registry.hasFunction(QStringLiteral("build_string")));
        QVERIFY(registry.hasFunction(QStringLiteral("print")));
        QVERIFY(registry.hasFunction(QStringLiteral("println")));
        QVERIFY(registry.hasFunction(QStringLiteral("scan")));
        QVERIFY(registry.hasFunction(QStringLiteral("str_to_chars")));
        QVERIFY(registry.hasFunction(QStringLiteral("chars_to_str")));
        QVERIFY(registry.hasFunction(QStringLiteral("debug_break")));
        QVERIFY(registry.hasFunction(QStringLiteral("debug_assert")));
        QVERIFY(registry.hasFunction(QStringLiteral("test_assert")));
        QVERIFY(registry.hasFunction(QStringLiteral("test_eq")));
        QVERIFY(registry.hasFunction(QStringLiteral("test_ne")));
        QVERIFY(registry.hasMethod(strType, QStringLiteral("len")));
        QVERIFY(registry.hasMethod(strType, QStringLiteral("empty")));
        QVERIFY(registry.hasMethod(strType, QStringLiteral("contains")));
        QVERIFY(registry.hasMethod(strType, QStringLiteral("find")));
        QVERIFY(registry.hasMethod(strType, QStringLiteral("substr")));
        QVERIFY(registry.hasMethod(strType, QStringLiteral("slice")));
        QVERIFY(registry.hasMethod(strType, QStringLiteral("replace")));

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

    void scanWritesPointerTargetsThroughRegistry()
    {
        auto registry = abel::BuiltinRegistry::makeDefault();
        abel::AbelRuntimeContext ctx;
        auto* intLoc = ctx.createStorage(abel::AbelValue::makeInt(0));
        auto* strLoc = ctx.createStorage(abel::AbelValue::makeString(QString()));
        auto* boolLoc = ctx.createStorage(abel::AbelValue::makeBool(false));
        auto* charLoc = ctx.createStorage(abel::AbelValue::makeChar(QChar()));
        auto* doubleLoc = ctx.createStorage(abel::AbelValue::makeDouble(0.0));
        QStringList tokens{QStringLiteral("42"), QStringLiteral("hakurei"), QStringLiteral("true"), QStringLiteral("z"), QStringLiteral("3.5")};

        abel::BuiltinFunctionCall call{
            ctx,
            QStringLiteral("scan"),
            {
                abel::AbelValue::makePointer(intLoc->read().type(), intLoc),
                abel::AbelValue::makePointer(strLoc->read().type(), strLoc),
                abel::AbelValue::makePointer(boolLoc->read().type(), boolLoc),
                abel::AbelValue::makePointer(charLoc->read().type(), charLoc),
                abel::AbelValue::makePointer(doubleLoc->read().type(), doubleLoc),
            },
            {},
            {},
        };
        call.readToken = [&tokens](const abel::SourceSpan&) -> std::optional<QString> {
            if (tokens.isEmpty())
                return std::nullopt;
            return tokens.takeFirst();
        };
        auto value = registry.callFunction(std::move(call));

        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(value.type().kind, abel::TypeKind::Void);
        QCOMPARE(intLoc->read().asInt(), 42);
        QCOMPARE(strLoc->read().asString(), QStringLiteral("hakurei"));
        QCOMPARE(boolLoc->read().asBool(), true);
        QCOMPARE(charLoc->read().asChar(), QChar('z'));
        QCOMPARE(doubleLoc->read().asDouble(), 3.5);
    }

    void scanReportsBadToken()
    {
        auto registry = abel::BuiltinRegistry::makeDefault();
        abel::AbelRuntimeContext ctx;
        auto* intLoc = ctx.createStorage(abel::AbelValue::makeInt(0));

        abel::BuiltinFunctionCall call{
            ctx,
            QStringLiteral("scan"),
            {abel::AbelValue::makePointer(intLoc->read().type(), intLoc)},
            {},
            {},
        };
        call.readToken = [](const abel::SourceSpan&) -> std::optional<QString> {
            return QStringLiteral("bad");
        };
        auto value = registry.callFunction(std::move(call));

        QVERIFY(!ctx.diagnostics().isEmpty());
        QCOMPARE(ctx.diagnostics().back().code, QStringLiteral("E0424"));
        QCOMPARE(value.type().kind, abel::TypeKind::Unknown);
    }

    void stringMethodsRunThroughRegistry()
    {
        auto registry = abel::BuiltinRegistry::makeDefault();
        abel::AbelRuntimeContext ctx;
        const abel::AbelValue text = abel::AbelValue::makeString(QStringLiteral("hakurei shrine"));

        abel::BuiltinMethodCall len{
            ctx,
            nullptr,
            text,
            QStringLiteral("len"),
            {},
            {},
            {},
        };
        auto length = registry.callMethod(std::move(len));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(length.asInt(), 14);

        abel::BuiltinMethodCall contains{
            ctx,
            nullptr,
            text,
            QStringLiteral("contains"),
            {abel::AbelValue::makeString(QStringLiteral("rei"))},
            {},
            {},
        };
        auto hasNeedle = registry.callMethod(std::move(contains));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(hasNeedle.asBool(), true);

        abel::BuiltinMethodCall find{
            ctx,
            nullptr,
            text,
            QStringLiteral("find"),
            {abel::AbelValue::makeString(QStringLiteral("shrine"))},
            {},
            {},
        };
        auto index = registry.callMethod(std::move(find));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(index.asInt(), 8);

        abel::BuiltinMethodCall slice{
            ctx,
            nullptr,
            text,
            QStringLiteral("slice"),
            {abel::AbelValue::makeInt(8), abel::AbelValue::makeInt(6)},
            {},
            {},
        };
        auto part = registry.callMethod(std::move(slice));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(part.asString(), QStringLiteral("shrine"));

        abel::BuiltinMethodCall replace{
            ctx,
            nullptr,
            text,
            QStringLiteral("replace"),
            {abel::AbelValue::makeString(QStringLiteral("shrine")), abel::AbelValue::makeString(QStringLiteral("engine"))},
            {},
            {},
        };
        auto replaced = registry.callMethod(std::move(replace));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(replaced.asString(), QStringLiteral("hakurei engine"));
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

    void debugAssertReportsOnlyWhenFalse()
    {
        auto registry = abel::BuiltinRegistry::makeDefault();
        abel::AbelRuntimeContext ctx;

        abel::BuiltinFunctionCall ok{
            ctx,
            QStringLiteral("debug_assert"),
            {abel::AbelValue::makeBool(true), abel::AbelValue::makeString(QStringLiteral("ignored"))},
            {},
            {},
        };
        auto okValue = registry.callFunction(std::move(ok));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(okValue.type().kind, abel::TypeKind::Void);

        abel::BuiltinFunctionCall fail{
            ctx,
            QStringLiteral("debug_assert"),
            {abel::AbelValue::makeBool(false), abel::AbelValue::makeString(QStringLiteral("x=")), abel::AbelValue::makeInt(7)},
            {},
            {},
        };
        auto failValue = registry.callFunction(std::move(fail));
        QVERIFY(!ctx.diagnostics().isEmpty());
        QCOMPARE(ctx.diagnostics().back().code, QStringLiteral("E0598"));
        QVERIFY(ctx.diagnostics().back().message.contains(QStringLiteral("x=7")));
        QCOMPARE(failValue.type().kind, abel::TypeKind::Unknown);
    }

    void testAssertionsReportFailures()
    {
        auto registry = abel::BuiltinRegistry::makeDefault();
        abel::AbelRuntimeContext ctx;

        abel::BuiltinFunctionCall okAssert{
            ctx,
            QStringLiteral("test_assert"),
            {abel::AbelValue::makeBool(true), abel::AbelValue::makeString(QStringLiteral("ignored"))},
            {},
            {},
        };
        auto okAssertValue = registry.callFunction(std::move(okAssert));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(okAssertValue.type().kind, abel::TypeKind::Void);

        abel::BuiltinFunctionCall okEq{
            ctx,
            QStringLiteral("test_eq"),
            {abel::AbelValue::makeInt(7), abel::AbelValue::makeDouble(7.0)},
            {},
            {},
        };
        auto okEqValue = registry.callFunction(std::move(okEq));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(okEqValue.type().kind, abel::TypeKind::Void);

        abel::BuiltinFunctionCall failEq{
            ctx,
            QStringLiteral("test_eq"),
            {abel::AbelValue::makeString(QStringLiteral("actual")), abel::AbelValue::makeString(QStringLiteral("expected")), abel::AbelValue::makeString(QStringLiteral(" label"))},
            {},
            {},
        };
        auto failEqValue = registry.callFunction(std::move(failEq));
        QVERIFY(!ctx.diagnostics().isEmpty());
        QCOMPARE(ctx.diagnostics().back().code, QStringLiteral("E0599"));
        QVERIFY(ctx.diagnostics().back().message.contains(QStringLiteral("test_eq failed")));
        QVERIFY(ctx.diagnostics().back().message.contains(QStringLiteral(" label")));
        QCOMPARE(failEqValue.type().kind, abel::TypeKind::Unknown);

        abel::AbelRuntimeContext neCtx;
        abel::BuiltinFunctionCall failNe{
            neCtx,
            QStringLiteral("test_ne"),
            {abel::AbelValue::makeInt(4), abel::AbelValue::makeInt(4)},
            {},
            {},
        };
        auto failNeValue = registry.callFunction(std::move(failNe));
        QVERIFY(!neCtx.diagnostics().isEmpty());
        QCOMPARE(neCtx.diagnostics().back().code, QStringLiteral("E0599"));
        QVERIFY(neCtx.diagnostics().back().message.contains(QStringLiteral("test_ne failed")));
        QCOMPARE(failNeValue.type().kind, abel::TypeKind::Unknown);
    }
};

QTEST_MAIN(AbelBuiltinRegistryTests)

#include "test_builtin_registry.moc"
