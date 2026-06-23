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
        QVERIFY(registry.hasMethod(vectorType, QStringLiteral("contains")));
        QVERIFY(registry.hasMethod(vectorType, QStringLiteral("count")));
        QVERIFY(registry.hasMethod(vectorType, QStringLiteral("extend")));
        QVERIFY(registry.hasMethod(vectorType, QStringLiteral("slice")));
        QVERIFY(registry.hasMethod(vectorType, QStringLiteral("sort")));
        QVERIFY(registry.hasMethod(vectorType, QStringLiteral("reverse")));
        QVERIFY(registry.hasMethod(vectorType, QStringLiteral("unique")));
        QVERIFY(registry.hasMethod(vectorType, QStringLiteral("binary_search")));
        QVERIFY(registry.hasMethod(vectorType, QStringLiteral("lower_bound")));
        QVERIFY(registry.hasMethod(vectorType, QStringLiteral("upper_bound")));
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

        abel::BuiltinMethodCall contains{
            ctx,
            loc,
            loc->read(),
            QStringLiteral("contains"),
            {abel::AbelValue::makeInt(2)},
            {},
            {},
        };
        auto hasTwo = registry.callMethod(std::move(contains));
        QVERIFY(ctx.diagnostics().isEmpty());
        QVERIFY(hasTwo.asBool());

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

        abel::BuiltinMethodCall count{
            ctx,
            loc,
            loc->read(),
            QStringLiteral("count"),
            {abel::AbelValue::makeInt(2)},
            {},
            {},
        };
        auto counted = registry.callMethod(std::move(count));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(counted.asInt(), 1);

        abel::BuiltinMethodCall slice{
            ctx,
            loc,
            loc->read(),
            QStringLiteral("slice"),
            {abel::AbelValue::makeInt(1), abel::AbelValue::makeInt(2)},
            {},
            {},
        };
        auto sliced = registry.callMethod(std::move(slice));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(sliced.type().kind, abel::TypeKind::Vector);
        QCOMPARE(sliced.asVector()->elements.size(), static_cast<size_t>(2));
        QCOMPARE(sliced.asVector()->elements[0].asInt(), 2);
        QCOMPARE(sliced.asVector()->elements[1].asInt(), 1);

        abel::BuiltinMethodCall extend{
            ctx,
            loc,
            loc->read(),
            QStringLiteral("extend"),
            {sliced},
            {},
            {},
        };
        auto extended = registry.callMethod(std::move(extend));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(extended.type().kind, abel::TypeKind::Void);
        QCOMPARE(loc->read().asVector()->elements.size(), static_cast<size_t>(5));
        QCOMPARE(loc->read().asVector()->elements[3].asInt(), 2);
        QCOMPARE(loc->read().asVector()->elements[4].asInt(), 1);

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
        QCOMPARE(loc->read().asVector()->elements[1].asInt(), 1);
        QCOMPARE(loc->read().asVector()->elements[2].asInt(), 2);

        abel::BuiltinMethodCall insertDuplicate{
            ctx,
            loc,
            loc->read(),
            QStringLiteral("insert"),
            {abel::AbelValue::makeInt(2), abel::AbelValue::makeInt(2)},
            {},
            {},
        };
        auto duplicated = registry.callMethod(std::move(insertDuplicate));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(duplicated.type().kind, abel::TypeKind::Void);

        abel::BuiltinMethodCall lowerBound{
            ctx,
            loc,
            loc->read(),
            QStringLiteral("lower_bound"),
            {abel::AbelValue::makeInt(2)},
            {},
            {},
        };
        auto lower = registry.callMethod(std::move(lowerBound));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(lower.asInt(), 2);

        abel::BuiltinMethodCall upperBound{
            ctx,
            loc,
            loc->read(),
            QStringLiteral("upper_bound"),
            {abel::AbelValue::makeInt(2)},
            {},
            {},
        };
        auto upper = registry.callMethod(std::move(upperBound));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(upper.asInt(), 5);

        abel::BuiltinMethodCall binarySearch{
            ctx,
            loc,
            loc->read(),
            QStringLiteral("binary_search"),
            {abel::AbelValue::makeInt(2)},
            {},
            {},
        };
        auto present = registry.callMethod(std::move(binarySearch));
        QVERIFY(ctx.diagnostics().isEmpty());
        QVERIFY(present.asBool());

        abel::BuiltinMethodCall binarySearchMissing{
            ctx,
            loc,
            loc->read(),
            QStringLiteral("binary_search"),
            {abel::AbelValue::makeInt(9)},
            {},
            {},
        };
        auto absentSearch = registry.callMethod(std::move(binarySearchMissing));
        QVERIFY(ctx.diagnostics().isEmpty());
        QVERIFY(!absentSearch.asBool());

        abel::BuiltinMethodCall unique{
            ctx,
            loc,
            loc->read(),
            QStringLiteral("unique"),
            {},
            {},
            {},
        };
        auto uniqued = registry.callMethod(std::move(unique));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(uniqued.type().kind, abel::TypeKind::Void);
        QCOMPARE(loc->read().asVector()->elements.size(), static_cast<size_t>(3));
        QCOMPARE(loc->read().asVector()->elements[0].asInt(), 1);
        QCOMPARE(loc->read().asVector()->elements[1].asInt(), 2);
        QCOMPARE(loc->read().asVector()->elements[2].asInt(), 3);

        abel::BuiltinMethodCall reverse{
            ctx,
            loc,
            loc->read(),
            QStringLiteral("reverse"),
            {},
            {},
            {},
        };
        auto reversed = registry.callMethod(std::move(reverse));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(reversed.type().kind, abel::TypeKind::Void);
        QCOMPARE(loc->read().asVector()->elements[0].asInt(), 3);
        QCOMPARE(loc->read().asVector()->elements[1].asInt(), 2);
        QCOMPARE(loc->read().asVector()->elements[2].asInt(), 1);

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
        QCOMPARE(loc->read().asVector()->elements[0].asInt(), 3);
        QCOMPARE(loc->read().asVector()->elements[1].asInt(), 1);
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
        QVERIFY(registry.hasFunction(QStringLiteral("abs")));
        QVERIFY(registry.hasFunction(QStringLiteral("sqrt")));
        QVERIFY(registry.hasFunction(QStringLiteral("floor")));
        QVERIFY(registry.hasFunction(QStringLiteral("ceil")));
        QVERIFY(registry.hasFunction(QStringLiteral("round")));
        QVERIFY(registry.hasFunction(QStringLiteral("trunc")));
        QVERIFY(registry.hasFunction(QStringLiteral("sin")));
        QVERIFY(registry.hasFunction(QStringLiteral("cos")));
        QVERIFY(registry.hasFunction(QStringLiteral("tan")));
        QVERIFY(registry.hasFunction(QStringLiteral("asin")));
        QVERIFY(registry.hasFunction(QStringLiteral("acos")));
        QVERIFY(registry.hasFunction(QStringLiteral("atan")));
        QVERIFY(registry.hasFunction(QStringLiteral("atan2")));
        QVERIFY(registry.hasFunction(QStringLiteral("exp")));
        QVERIFY(registry.hasFunction(QStringLiteral("log")));
        QVERIFY(registry.hasFunction(QStringLiteral("log10")));
        QVERIFY(registry.hasFunction(QStringLiteral("pow")));
        QVERIFY(registry.hasFunction(QStringLiteral("gcd")));
        QVERIFY(registry.hasFunction(QStringLiteral("lcm")));
        QVERIFY(registry.hasFunction(QStringLiteral("min")));
        QVERIFY(registry.hasFunction(QStringLiteral("max")));
        QVERIFY(registry.hasFunction(QStringLiteral("clamp")));
        QVERIFY(registry.hasFunction(QStringLiteral("debug_break")));
        QVERIFY(registry.hasFunction(QStringLiteral("debug_assert")));
        QVERIFY(registry.hasFunction(QStringLiteral("test_assert")));
        QVERIFY(registry.hasFunction(QStringLiteral("test_eq")));
        QVERIFY(registry.hasFunction(QStringLiteral("test_ne")));
        QVERIFY(registry.hasMethod(strType, QStringLiteral("len")));
        QVERIFY(registry.hasMethod(strType, QStringLiteral("empty")));
        QVERIFY(registry.hasMethod(strType, QStringLiteral("contains")));
        QVERIFY(registry.hasMethod(strType, QStringLiteral("find")));
        QVERIFY(registry.hasMethod(strType, QStringLiteral("starts_with")));
        QVERIFY(registry.hasMethod(strType, QStringLiteral("ends_with")));
        QVERIFY(registry.hasMethod(strType, QStringLiteral("substr")));
        QVERIFY(registry.hasMethod(strType, QStringLiteral("slice")));
        QVERIFY(registry.hasMethod(strType, QStringLiteral("replace")));
        QVERIFY(registry.hasMethod(strType, QStringLiteral("trim")));
        QVERIFY(registry.hasMethod(strType, QStringLiteral("lower")));
        QVERIFY(registry.hasMethod(strType, QStringLiteral("upper")));
        QVERIFY(registry.hasMethod(strType, QStringLiteral("split")));

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

    void mathBuiltinsRunThroughRegistry()
    {
        auto registry = abel::BuiltinRegistry::makeDefault();
        abel::AbelRuntimeContext ctx;

        auto call = [&](const QString& name, std::vector<abel::AbelValue> args) {
            return registry.callFunction(abel::BuiltinFunctionCall{ctx, name, std::move(args), {}, {}});
        };

        QCOMPARE(call(QStringLiteral("abs"), {abel::AbelValue::makeInt(-7)}).asInt(), 7);
        QCOMPARE(call(QStringLiteral("sqrt"), {abel::AbelValue::makeInt(9)}).asDouble(), 3.0);
        QCOMPARE(call(QStringLiteral("floor"), {abel::AbelValue::makeDouble(3.9)}).asDouble(), 3.0);
        QCOMPARE(call(QStringLiteral("ceil"), {abel::AbelValue::makeDouble(3.1)}).asDouble(), 4.0);
        QCOMPARE(call(QStringLiteral("round"), {abel::AbelValue::makeDouble(3.5)}).asDouble(), 4.0);
        QCOMPARE(call(QStringLiteral("trunc"), {abel::AbelValue::makeDouble(3.9)}).asDouble(), 3.0);
        QCOMPARE(call(QStringLiteral("pow"), {abel::AbelValue::makeInt(2), abel::AbelValue::makeInt(5)}).asDouble(), 32.0);
        QCOMPARE(call(QStringLiteral("sin"), {abel::AbelValue::makeDouble(0.0)}).asDouble(), 0.0);
        QCOMPARE(call(QStringLiteral("cos"), {abel::AbelValue::makeDouble(0.0)}).asDouble(), 1.0);
        QCOMPARE(call(QStringLiteral("tan"), {abel::AbelValue::makeDouble(0.0)}).asDouble(), 0.0);
        QCOMPARE(call(QStringLiteral("asin"), {abel::AbelValue::makeDouble(0.0)}).asDouble(), 0.0);
        QCOMPARE(call(QStringLiteral("acos"), {abel::AbelValue::makeDouble(1.0)}).asDouble(), 0.0);
        QCOMPARE(call(QStringLiteral("atan"), {abel::AbelValue::makeDouble(0.0)}).asDouble(), 0.0);
        QCOMPARE(call(QStringLiteral("atan2"), {abel::AbelValue::makeDouble(0.0), abel::AbelValue::makeDouble(1.0)}).asDouble(), 0.0);
        QCOMPARE(call(QStringLiteral("exp"), {abel::AbelValue::makeDouble(0.0)}).asDouble(), 1.0);
        QCOMPARE(call(QStringLiteral("log"), {abel::AbelValue::makeDouble(1.0)}).asDouble(), 0.0);
        QCOMPARE(call(QStringLiteral("log10"), {abel::AbelValue::makeDouble(100.0)}).asDouble(), 2.0);
        QCOMPARE(call(QStringLiteral("gcd"), {abel::AbelValue::makeInt(54), abel::AbelValue::makeInt(24)}).asInt(), 6);
        QCOMPARE(call(QStringLiteral("lcm"), {abel::AbelValue::makeInt(6), abel::AbelValue::makeInt(8)}).asInt(), 24);
        QCOMPARE(call(QStringLiteral("min"), {abel::AbelValue::makeInt(4), abel::AbelValue::makeDouble(5.5)}).asDouble(), 4.0);
        QCOMPARE(call(QStringLiteral("max"), {abel::AbelValue::makeInt(2), abel::AbelValue::makeInt(3)}).asInt(), 3);
        QCOMPARE(call(QStringLiteral("clamp"), {abel::AbelValue::makeInt(10), abel::AbelValue::makeInt(0), abel::AbelValue::makeInt(7)}).asInt(), 7);
        QVERIFY(ctx.diagnostics().isEmpty());
    }

    void mathClampReportsBadBounds()
    {
        auto registry = abel::BuiltinRegistry::makeDefault();
        abel::AbelRuntimeContext ctx;
        abel::BuiltinFunctionCall call{
            ctx,
            QStringLiteral("clamp"),
            {abel::AbelValue::makeInt(1), abel::AbelValue::makeInt(9), abel::AbelValue::makeInt(2)},
            {},
            {},
        };
        auto value = registry.callFunction(std::move(call));

        QVERIFY(!ctx.diagnostics().isEmpty());
        QCOMPARE(ctx.diagnostics().back().code, QStringLiteral("E0432"));
        QCOMPARE(value.type().kind, abel::TypeKind::Unknown);
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

        auto call = [&](const abel::AbelValue& receiver, const QString& name, std::vector<abel::AbelValue> args = {}) {
            return registry.callMethod(abel::BuiltinMethodCall{ctx, nullptr, receiver, name, std::move(args), {}, {}});
        };

        auto length = call(text, QStringLiteral("len"));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(length.asInt(), 14);

        auto hasNeedle = call(text, QStringLiteral("contains"), {abel::AbelValue::makeString(QStringLiteral("rei"))});
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(hasNeedle.asBool(), true);

        auto index = call(text, QStringLiteral("find"), {abel::AbelValue::makeString(QStringLiteral("shrine"))});
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(index.asInt(), 8);

        auto starts = call(text, QStringLiteral("starts_with"), {abel::AbelValue::makeString(QStringLiteral("hak"))});
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(starts.asBool(), true);

        auto ends = call(text, QStringLiteral("ends_with"), {abel::AbelValue::makeString(QStringLiteral("shrine"))});
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(ends.asBool(), true);

        auto part = call(text, QStringLiteral("slice"), {abel::AbelValue::makeInt(8), abel::AbelValue::makeInt(6)});
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(part.asString(), QStringLiteral("shrine"));

        auto replaced = call(text,
                             QStringLiteral("replace"),
                             {abel::AbelValue::makeString(QStringLiteral("shrine")),
                              abel::AbelValue::makeString(QStringLiteral("engine"))});
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(replaced.asString(), QStringLiteral("hakurei engine"));

        const abel::AbelValue padded = abel::AbelValue::makeString(QStringLiteral("  Aya  "));
        auto trimmed = call(padded, QStringLiteral("trim"));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(trimmed.asString(), QStringLiteral("Aya"));

        auto lower = call(abel::AbelValue::makeString(QStringLiteral("Aya")), QStringLiteral("lower"));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(lower.asString(), QStringLiteral("aya"));

        auto upper = call(lower, QStringLiteral("upper"));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(upper.asString(), QStringLiteral("AYA"));

        auto split = call(text, QStringLiteral("split"), {abel::AbelValue::makeString(QStringLiteral(" "))});
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(split.type().kind, abel::TypeKind::Vector);
        QVERIFY(split.type().pointee);
        QCOMPARE(split.type().pointee->kind, abel::TypeKind::Str);
        QCOMPARE(split.asVector()->elements.size(), static_cast<size_t>(2));
        QCOMPARE(split.asVector()->elements[0].asString(), QStringLiteral("hakurei"));
        QCOMPARE(split.asVector()->elements[1].asString(), QStringLiteral("shrine"));

        auto joined = call(abel::AbelValue::makeString(QStringLiteral("/")),
                           QStringLiteral("join"),
                           {split});
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(joined.asString(), QStringLiteral("hakurei/shrine"));

        auto parsedInt = call(abel::AbelValue::makeString(QStringLiteral("123")), QStringLiteral("parse_int"));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(parsedInt.type().kind, abel::TypeKind::I32);
        QCOMPARE(parsedInt.asInt(), 123);

        auto parsedLong = call(abel::AbelValue::makeString(QStringLiteral("5000000000")), QStringLiteral("parse_long"));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(parsedLong.type().kind, abel::TypeKind::I64);
        QCOMPARE(parsedLong.asInt(), 5000000000LL);

        auto parsedDouble = call(abel::AbelValue::makeString(QStringLiteral("2.5")), QStringLiteral("parse_double"));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(parsedDouble.type().kind, abel::TypeKind::F64);
        QCOMPARE(parsedDouble.asDouble(), 2.5);

        auto parsedBool = call(abel::AbelValue::makeString(QStringLiteral("true")), QStringLiteral("parse_bool"));
        QVERIFY(ctx.diagnostics().isEmpty());
        QCOMPARE(parsedBool.type().kind, abel::TypeKind::Bool);
        QVERIFY(parsedBool.asBool());
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
