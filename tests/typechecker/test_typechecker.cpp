#include "abelcore/lexer.h"
#include "abelcore/parser.h"
#include "abelcore/typechecker.h"

#include <QtTest/QtTest>

class AbelTypeCheckerTests final : public QObject {
    Q_OBJECT

private:
    static abel::TypeCheckResult checkSource(const QString& src)
    {
        abel::Lexer lexer;
        auto lexed = lexer.lex(QStringLiteral("<test>"), src);
        for (const auto& d : lexed.diagnostics)
            qWarning() << d.code << d.message;
        if (!lexed.diagnostics.isEmpty())
            return {lexed.diagnostics};

        abel::Parser parser;
        auto parsed = parser.parse(lexed.tokens);
        for (const auto& d : parsed.diagnostics)
            qWarning() << d.code << d.message;
        if (!parsed.diagnostics.isEmpty())
            return {parsed.diagnostics};

        abel::TypeChecker checker;
        return checker.check(*parsed.program);
    }

    static int countMessagesContaining(const abel::TypeCheckResult& result, const QString& needle)
    {
        int count = 0;
        for (const auto& d : result.diagnostics) {
            if (d.message.contains(needle))
                ++count;
        }
        return count;
    }

private slots:
    void acceptsArithmeticMain()
    {
        auto result = checkSource(QStringLiteral("fn int main() { return 1 + 2 * 3; }"));
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void acceptsFixedWidthIntegerTypes()
    {
        const QString src = QStringLiteral(R"(
            fn int take_i16(i16 x) {
                return x;
            }

            fn int main() {
                i8 a = -1;
                i16 b = a + 2;
                i32 c = b + 3;
                i64 d = cast<i64>(c);

                u8 e = 250;
                u16 f = e + 10;
                u32 g = cast<u32>(f);
                u64 h = cast<u64>(g);

                vector<u16> xs = {e, f};
                str s = build_string(a, b, c, d, e, f, g, h, xs.len());
                return take_i16(b) + cast<int>(h) + xs.len();
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void acceptsEnumAndTypeAlias()
    {
        const QString src = QStringLiteral(R"(
            enum Color {
                Red,
                Green,
                Blue,
            }

            type Index = int;
            type Scores = vector<Index>;
            type Favorite = Color;

            fn int main() {
                Scores xs = {1, 2};
                Favorite c = Color.Green;
                return c + xs[1];
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void rejectsBadEnumAndRecursiveAlias()
    {
        auto badEnumerator = checkSource(QStringLiteral(R"(
            enum Color { Red, Green }

            fn int main() {
                return Color.Blue;
            }
        )"));
        QVERIFY(!badEnumerator.diagnostics.isEmpty());

        auto recursiveAlias = checkSource(QStringLiteral(R"(
            type A = B;
            type B = A;

            fn int main() {
                A x = 0;
                return x;
            }
        )"));
        QVERIFY(!recursiveAlias.diagnostics.isEmpty());
    }

    void rejectsIntegerIfCondition()
    {
        auto result = checkSource(QStringLiteral("fn int main() { if (1) { return 1; } return 0; }"));
        QVERIFY(!result.diagnostics.isEmpty());
    }

    void rejectsReferenceBindingToPrvalue()
    {
        auto result = checkSource(QStringLiteral("fn int main() { int& r = 1; return 0; }"));
        QVERIFY(!result.diagnostics.isEmpty());
    }

    void acceptsVectorReferenceAndBuiltins()
    {
        const QString src = QStringLiteral(R"(
            fn void mutate(vector<int>& xs) {
                xs.push(3);
            }

            fn int main() {
                vector<int> xs = {1, 2};
                mutate(xs);
                str s = build_string("len=", xs.len());
                return xs.len();
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void acceptsConstReferenceReadOnlyBindings()
    {
        const QString src = QStringLiteral(R"(
            fn int read(const int& x) {
                return x;
            }

            fn int main() {
                int a = 4;
                const int b = 5;
                func int(const int&) f = lambda [] int(const int& x) {
                    return x;
                };
                return read(a) + read(b) + f(a);
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void rejectsConstReferenceMutationAndBadBinding()
    {
        auto mutate = checkSource(QStringLiteral(R"(
            fn void bad(const int& x) {
                x = 1;
            }

            fn int main() {
                int a = 0;
                bad(a);
                return 0;
            }
        )"));
        QVERIFY(!mutate.diagnostics.isEmpty());

        auto bindConstToMutableRef = checkSource(QStringLiteral(R"(
            fn void inc(int& x) {
                x = x + 1;
            }

            fn int main() {
                const int a = 1;
                inc(a);
                return 0;
            }
        )"));
        QVERIFY(!bindConstToMutableRef.diagnostics.isEmpty());

        auto bindPrvalue = checkSource(QStringLiteral("fn int main() { const int& r = 4; return 0; }"));
        QVERIFY(!bindPrvalue.diagnostics.isEmpty());
    }

    void acceptsStringCharConversions()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                vector<char> cs = str_to_chars("ab");
                str s = chars_to_str(cs);
                return cs.len();
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void acceptsCastPipeAndExtendedOperators()
    {
        const QString src = QStringLiteral(R"(
            fn int first(any... args) {
                return cast<int>(args[0]);
            }

            fn int main() {
                func int(int, int) add = lambda [] int(int a, int b) {
                    return a + b;
                };
                int piped = 4 |> add(5);
                str roundtrip = "az" |> str_to_chars |> chars_to_str;
                int x = first(7);
                return (2 ** 3) + (-5 %% 3) + (x <? 10) + (piped >? 4);
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void acceptsPrvalueVectorReceiverAndNumericConversions()
    {
        const QString src = QStringLiteral(R"(
            fn int take_int(int x) {
                return x;
            }

            backend MathSystem {
                fn int accept_int(int x);
            }

            fn int main() {
                str ans = "abc";
                int len = 5;
                while (str_to_chars(ans).len() < len) {
                    len = len - 10;
                }

                str_to_chars(ans).push('d');
                char c = str_to_chars(ans).front();

                int a = 4.6;
                int b = cast<int>(4.6);
                double d = cast<double>(a);
                int e = take_int(3.9);
                return MathSystem::accept_int(d) + a + b + e;
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void acceptsSourceLocationBuiltinsWithoutLambdaCapture()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                func int() f = lambda [] int() {
                    int line = __LINE__;
                    int column = __COLUMN__;
                    str file = __FILE__;
                    if (file == "<test>") {
                        return line + column;
                    }
                    return 0;
                };
                return f();
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void acceptsDebugBuiltins()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                debug_assert(true);
                debug_assert(1 < 2, "line=", __LINE__);
                true |> debug_assert(" via pipe");
                return 0;
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void rejectsDebugBuiltinMisuse()
    {
        auto badCond = checkSource(QStringLiteral("fn int main() { debug_assert(1); return 0; }"));
        QVERIFY(!badCond.diagnostics.isEmpty());
        auto badBreakArg = checkSource(QStringLiteral("fn int main() { debug_break(\"bad\"); return 0; }"));
        QVERIFY(!badBreakArg.diagnostics.isEmpty());
    }

    void acceptsTestBuiltins()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                test_assert(true, "ok");
                test_eq(4, 4.0, "numeric ok");
                test_ne("a", "b");
                true |> test_assert(" via pipe");
                4 |> test_eq(4, " pipe eq");
                return 0;
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void rejectsTestBuiltinMisuse()
    {
        auto badCond = checkSource(QStringLiteral("fn int main() { test_assert(1); return 0; }"));
        QVERIFY(!badCond.diagnostics.isEmpty());
        auto tooFew = checkSource(QStringLiteral("fn int main() { test_eq(1); return 0; }"));
        QVERIFY(!tooFew.diagnostics.isEmpty());
        auto badCompare = checkSource(QStringLiteral("fn int main() { test_eq(1, \"x\"); return 0; }"));
        QVERIFY(!badCompare.diagnostics.isEmpty());
    }

    void acceptsUserToStrForBuildString()
    {
        const QString src = QStringLiteral(R"ABEL(
            struct Student {
                str name;
                int age;
            }

            fn str to_str(Student s) {
                return build_string(s.name, "(", s.age, ")");
            }

            fn int main() {
                Student s = Student("Aya", 16);
                str text = build_string("student=", s);
                return 0;
            }
        )ABEL");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void rejectsStructBuildStringWithoutToStr()
    {
        const QString src = QStringLiteral(R"ABEL(
            struct Student {
                str name;
            }

            fn int main() {
                Student s = Student("Aya");
                str text = build_string(s);
                return 0;
            }
        )ABEL");
        auto result = checkSource(src);
        QVERIFY(!result.diagnostics.isEmpty());
    }

    void rejectsCastFromNonAnyNonNumeric()
    {
        auto result = checkSource(QStringLiteral("fn int main() { int x = cast<int>(\"bad\"); return x; }"));
        QVERIFY(!result.diagnostics.isEmpty());
    }

    void rejectsCharsToStrWithNonCharVector()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                vector<int> xs = {1, 2};
                str s = chars_to_str(xs);
                return 0;
            }
        )");
        auto result = checkSource(src);
        QVERIFY(!result.diagnostics.isEmpty());
    }

    void rejectsBadVectorPushArgument()
    {
        auto result = checkSource(QStringLiteral(R"(
            fn int main() {
                vector<int> xs = {1};
                xs.push("bad");
                return 0;
            }
        )"));
        QVERIFY(!result.diagnostics.isEmpty());
    }

    void rejectsUnknownFunction()
    {
        auto result = checkSource(QStringLiteral("fn int main() { return nope(); }"));
        QVERIFY(!result.diagnostics.isEmpty());
    }

    void suppressesUnknownReturnAndBinaryCascades()
    {
        auto result = checkSource(QStringLiteral("fn int main() { return nope() + 1; }"));
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QCOMPARE(result.diagnostics.size(), 1);
        QVERIFY(result.diagnostics.front().message.contains(QStringLiteral("unknown function 'nope'")));
        QCOMPARE(countMessagesContaining(result, QStringLiteral("operator")), 0);
        QCOMPARE(countMessagesContaining(result, QStringLiteral("cannot return")), 0);
    }

    void suppressesUnknownConditionCascade()
    {
        auto result = checkSource(QStringLiteral(R"(
            fn int main() {
                if (missing()) {
                    return 1;
                }
                return 0;
            }
        )"));
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QCOMPARE(result.diagnostics.size(), 1);
        QVERIFY(result.diagnostics.front().message.contains(QStringLiteral("unknown function 'missing'")));
        QCOMPARE(countMessagesContaining(result, QStringLiteral("condition must be bool")), 0);
    }

    void keepsIndependentUnknownSiblingDiagnostics()
    {
        auto result = checkSource(QStringLiteral(R"(
            fn int main() {
                int a = nope();
                int b = other();
                return 0;
            }
        )"));
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QCOMPARE(result.diagnostics.size(), 2);
        QCOMPARE(countMessagesContaining(result, QStringLiteral("unknown function 'nope'")), 1);
        QCOMPARE(countMessagesContaining(result, QStringLiteral("unknown function 'other'")), 1);
        QCOMPARE(countMessagesContaining(result, QStringLiteral("cannot initialize")), 0);
    }

    void suppressesUnknownReceiverMethodCascade()
    {
        auto result = checkSource(QStringLiteral("fn int main() { return missingVector().len(); }"));
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QCOMPARE(result.diagnostics.size(), 1);
        QVERIFY(result.diagnostics.front().message.contains(QStringLiteral("unknown function 'missingVector'")));
        QCOMPARE(countMessagesContaining(result, QStringLiteral("requires vector receiver")), 0);
    }

    void rejectsMissingReturnInFunctionAtCheckTime()
    {
        const QString src = QStringLiteral(R"(
            fn int f() {
                int x = 1;
            }

            fn int main() {
                return 0;
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QCOMPARE(result.diagnostics.size(), 1);
        QVERIFY(result.diagnostics.front().message.contains(QStringLiteral("function 'f' may end without returning int")));
    }

    void acceptsDefiniteReturnThroughIfElseAndInfiniteWhile()
    {
        const QString src = QStringLiteral(R"(
            fn int choose(bool b) {
                if (b) {
                    return 1;
                } else {
                    return 2;
                }
            }

            fn int spin() {
                while (true) {
                    return 3;
                }
            }

            fn int main() {
                return choose(true) + spin();
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void rejectsMissingReturnInMethodAndLambdaAtCheckTime()
    {
        const QString src = QStringLiteral(R"(
            struct Box {
                fn int get() {
                    int x = 1;
                }
            }

            fn int main() {
                func int() f = lambda [] int() {
                    int y = 2;
                };
                return 0;
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QCOMPARE(result.diagnostics.size(), 2);
        QCOMPARE(countMessagesContaining(result, QStringLiteral("method 'get' may end without returning int")), 1);
        QCOMPARE(countMessagesContaining(result, QStringLiteral("lambda may end without returning int")), 1);
    }

    void suppressesMissingReturnWhenBodyAlreadyHasRootError()
    {
        const QString src = QStringLiteral(R"(
            fn int f() {
                missing();
            }

            fn int main() {
                return 0;
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QCOMPARE(result.diagnostics.size(), 1);
        QVERIFY(result.diagnostics.front().message.contains(QStringLiteral("unknown function 'missing'")));
        QCOMPARE(countMessagesContaining(result, QStringLiteral("may end without returning")), 0);
    }

    void rejectsBreakOutsideLoop()
    {
        auto result = checkSource(QStringLiteral("fn int main() { break; return 0; }"));
        QVERIFY(!result.diagnostics.isEmpty());
    }

    void acceptsForAndRangeFor()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                int sum = 0;
                for (int i = 0; i < 3; i = i + 1) {
                    sum = sum + i;
                }
                vector<int> xs = {1, 2};
                for (x in xs) {
                    x = x + sum;
                }
                return xs[1];
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void rejectsForConditionThatIsNotBool()
    {
        auto result = checkSource(QStringLiteral("fn int main() { for (int i = 0; i; i = i + 1) { } return 0; }"));
        QVERIFY(!result.diagnostics.isEmpty());
    }

    void rejectsRangeForNonVector()
    {
        auto result = checkSource(QStringLiteral("fn int main() { int x = 0; for (a in x) { } return 0; }"));
        QVERIFY(!result.diagnostics.isEmpty());
    }

    void acceptsVectorResizeClearAndFrontBackLvalues()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                vector<int> xs = {1};
                xs.reserve(8);
                xs.resize(3);
                xs.front() = 4;
                xs.back() = xs.front() + 5;
                xs.clear();
                return 0;
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void rejectsVectorResizeWithNonIntegerSize()
    {
        auto result = checkSource(QStringLiteral("fn int main() { vector<int> xs = {1}; xs.resize(\"bad\"); return 0; }"));
        QVERIFY(!result.diagnostics.isEmpty());
    }

    void acceptsVectorStructResizeWhenElementDefaultConstructible()
    {
        const QString src = QStringLiteral(R"(
            struct Point {
                int x;
                int y;
            }

            fn int main() {
                vector<Point> xs;
                xs.resize(2);
                return 0;
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void acceptsZeroArgInitAsDefaultConstructor()
    {
        const QString src = QStringLiteral(R"(
            struct Point {
                int x;

                init() {
                    x = 7;
                }
            }

            fn int main() {
                vector<Point> xs;
                xs.resize(2);
                Point p;
                Point q = Point();
                return 0;
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void rejectsVectorStructResizeWhenElementNeedsConstructorArgs()
    {
        const QString src = QStringLiteral(R"(
            struct Complex {
                int x;

                init(int v) {
                    x = v;
                }
            }

            fn int main() {
                vector<Complex> xs;
                xs.resize(2);
                Complex c;
                return 0;
            }
        )");
        auto result = checkSource(src);
        QVERIFY(!result.diagnostics.isEmpty());
    }

    void rejectsAssigningConstVectorFront()
    {
        auto result = checkSource(QStringLiteral("fn int main() { const vector<int> xs = {1}; xs.front() = 2; return 0; }"));
        QVERIFY(!result.diagnostics.isEmpty());
    }

    void rejectsReadonlyContainerAndStructLocations()
    {
        auto constIndex = checkSource(QStringLiteral("fn int main() { const vector<int> xs = {1}; xs[0] = 2; return 0; }"));
        QVERIFY(!constIndex.diagnostics.isEmpty());

        auto constRange = checkSource(QStringLiteral(R"(
            fn int main() {
                const vector<int> xs = {1, 2};
                for (x in xs) {
                    x = x + 1;
                }
                return 0;
            }
        )"));
        QVERIFY(!constRange.diagnostics.isEmpty());

        auto constField = checkSource(QStringLiteral(R"(
            struct Hold {
                const int x;
            }

            fn int main() {
                Hold h = Hold(1);
                h.x = 2;
                return 0;
            }
        )"));
        QVERIFY(!constField.diagnostics.isEmpty());

        auto constReceiver = checkSource(QStringLiteral(R"(
            struct Box {
                int x;

                init(int start) {
                    x = start;
                }

                fn void inc() {
                    x = x + 1;
                }
            }

            fn int main() {
                const Box b = Box(1);
                b.inc();
                return 0;
            }
        )"));
        QVERIFY(!constReceiver.diagnostics.isEmpty());
    }

    void acceptsStructCounter()
    {
        const QString src = QStringLiteral(R"(
            struct Counter {
                int value;

                init(int start) {
                    value = start;
                }

                fn void inc() {
                    value = value + 1;
                }

                const fn int get() {
                    return value;
                }
            }

            fn int main() {
                Counter c = Counter(0);
                c.inc();
                return c.get();
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void rejectsUnknownStructField()
    {
        const QString src = QStringLiteral(R"(
            struct Box {
                int x;
            }
            fn int main() {
                Box b = Box(1);
                return b.y;
            }
        )");
        auto result = checkSource(src);
        QVERIFY(!result.diagnostics.isEmpty());
    }

    void acceptsFuncTypeAndLambdaCaptures()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                int x = 1;
                int y = 2;
                func int() h = lambda [x, &y] int() {
                    y = y + 1;
                    return x + y;
                };
                return h();
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void rejectsUncapturedLambdaVariable()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                int x = 1;
                func int() f = lambda [] int() {
                    return x;
                };
                return f();
            }
        )");
        auto result = checkSource(src);
        QVERIFY(!result.diagnostics.isEmpty());
    }

    void rejectsFunctionValueWrongArgument()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                func int(int) f = lambda [] int(int x) {
                    return x;
                };
                return f("bad");
            }
        )");
        auto result = checkSource(src);
        QVERIFY(!result.diagnostics.isEmpty());
    }

    void rejectsBreakInsideLambdaEvenWhenCreatedInLoop()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                while (true) {
                    func void() f = lambda [] void() {
                        break;
                    };
                    return 0;
                }
                return 0;
            }
        )");
        auto result = checkSource(src);
        QVERIFY(!result.diagnostics.isEmpty());
    }

    void acceptsBackendStaticCallSignature()
    {
        const QString src = QStringLiteral(R"(
            backend MathSystem {
                fn int fast_add(int a, int b);
            }

            fn int main() {
                return MathSystem::fast_add(1, 2);
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void rejectsBadBackendStaticCallArgument()
    {
        const QString src = QStringLiteral(R"(
            backend MathSystem {
                fn int fast_add(int a, int b);
            }

            fn int main() {
                return MathSystem::fast_add(1, "bad");
            }
        )");
        auto result = checkSource(src);
        QVERIFY(!result.diagnostics.isEmpty());
    }

    void rejectsUnknownBackendFunction()
    {
        const QString src = QStringLiteral(R"(
            backend MathSystem {
                fn int fast_add(int a, int b);
            }

            fn int main() {
                return MathSystem::missing(1, 2);
            }
        )");
        auto result = checkSource(src);
        QVERIFY(!result.diagnostics.isEmpty());
    }
};

QTEST_MAIN(AbelTypeCheckerTests)

#include "test_typechecker.moc"
