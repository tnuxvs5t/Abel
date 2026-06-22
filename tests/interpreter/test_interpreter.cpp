#include "abelcore/interpreter.h"
#include "abelcore/lexer.h"
#include "abelcore/parser.h"

#include <QtTest/QtTest>

class AbelInterpreterTests final : public QObject {
    Q_OBJECT

private:
    static abel::InterpreterResult runSource(const QString& src)
    {
        abel::Lexer lexer;
        auto lexed = lexer.lex(QStringLiteral("<test>"), src);
        for (const auto& d : lexed.diagnostics)
            qWarning() << d.code << d.message;
        if (!lexed.diagnostics.isEmpty()) {
            abel::InterpreterResult result;
            result.exitCode = 1;
            result.diagnostics = lexed.diagnostics;
            return result;
        }

        abel::Parser parser;
        auto parsed = parser.parse(lexed.tokens);
        for (const auto& d : parsed.diagnostics)
            qWarning() << d.code << d.message;
        if (!parsed.diagnostics.isEmpty()) {
            abel::InterpreterResult result;
            result.exitCode = 1;
            result.diagnostics = parsed.diagnostics;
            return result;
        }

        abel::Interpreter interpreter;
        return interpreter.run(*parsed.program);
    }

    static bool stackHasSymbol(const abel::Diagnostic& diagnostic, const QString& symbol)
    {
        for (const auto& frame : diagnostic.stackTrace) {
            if (frame.symbol == symbol)
                return true;
        }
        return false;
    }

    static int stackIndexOf(const abel::Diagnostic& diagnostic, const QString& symbol)
    {
        for (qsizetype i = 0; i < diagnostic.stackTrace.size(); ++i) {
            if (diagnostic.stackTrace[i].symbol == symbol)
                return static_cast<int>(i);
        }
        return -1;
    }

private slots:
    void returnsArithmetic()
    {
        auto result = runSource(QStringLiteral("fn int main() { return 1 + 2 * 3; }"));
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 7);
    }

    void handlesVariablesAndLoops()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                int x = 0;
                repeat(3) {
                    x = x + 2;
                }
                while (x < 10) {
                    x = x + 1;
                }
                if (x == 10) {
                    return x;
                } else {
                    return 0;
                }
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 10);
    }

    void negativeRepeatExecutesZeroTimes()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                int x = 7;
                repeat(-3) {
                    x = 99;
                }
                return x;
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 7);
    }

    void callsUserFunction()
    {
        const QString src = QStringLiteral(R"(
            fn int add(int a, int b) {
                return a + b;
            }

            fn int main() {
                return add(4, 5);
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 9);
    }

    void rejectsIntegerCondition()
    {
        auto result = runSource(QStringLiteral("fn int main() { if (1) { return 1; } return 0; }"));
        QVERIFY(!result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 1);
    }

    void rejectsBreakOutsideLoop()
    {
        auto result = runSource(QStringLiteral("fn int main() { break; return 0; }"));
        QVERIFY(!result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 1);
    }

    void supportsMixedNumericEquality()
    {
        auto result = runSource(QStringLiteral("fn int main() { long x = 3; if (x == 3) { return 11; } return 0; }"));
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 11);
    }

    void referenceWritesBack()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                int x = 0;
                int& r = x;
                r = 5;
                return x;
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 5);
    }

    void pointerDereferenceWritesBack()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                int x = 1;
                int* p = &x;
                *p = *p + 9;
                return x;
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 10);
    }

    void referenceParameterWritesBack()
    {
        const QString src = QStringLiteral(R"(
            fn void inc(int& x) {
                x = x + 1;
            }

            fn int main() {
                int v = 4;
                inc(v);
                return v;
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 5);
    }

    void comparesNullptr()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                int* p = nullptr;
                if (p == nullptr) {
                    return 3;
                }
                return 0;
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 3);
    }

    void rejectsUninitializedReference()
    {
        auto result = runSource(QStringLiteral("fn int main() { int& r; return 0; }"));
        QVERIFY(!result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 1);
    }

    void rejectsReferenceToPrvalue()
    {
        auto result = runSource(QStringLiteral("fn int main() { int& r = 1; return 0; }"));
        QVERIFY(!result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 1);
    }

    void vectorIndexWritesBack()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                vector<int> xs = {1, 2, 3};
                xs[1] = 10;
                return xs[1];
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 10);
    }

    void vectorMethodsWork()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                vector<int> xs = {1};
                xs.push(4);
                xs.push(7);
                return xs.len() + xs.back();
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 10);
    }

    void vectorResizeReserveClearWork()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                vector<int> xs = {5};
                xs.reserve(10);
                xs.resize(3);
                int before = xs.len() + xs[0] + xs[2];
                xs.clear();
                if (xs.empty()) {
                    return before;
                }
                return 0;
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 8);
    }

    void vectorStructResizeDefaultConstructsElements()
    {
        const QString src = QStringLiteral(R"(
            struct Point {
                int x;
                int y;
            }

            fn int main() {
                vector<Point> xs;
                xs.resize(2);
                xs[0].x = 5;
                xs[1].y = 7;
                return xs[0].x + xs[0].y + xs[1].x + xs[1].y;
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 12);
    }

    void vectorStructResizeRunsZeroArgInit()
    {
        const QString src = QStringLiteral(R"(
            struct Counter {
                int value;

                init() {
                    value = 3;
                }
            }

            fn int main() {
                vector<Counter> xs;
                xs.resize(2);
                Counter c;
                return xs[0].value + xs[1].value + c.value;
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 9);
    }

    void explicitFieldConstructorStillWorksWithoutDefaultConstruction()
    {
        const QString src = QStringLiteral(R"(
            struct NeedsValue {
                int x;
            }

            fn int main() {
                NeedsValue v = NeedsValue(4);
                return v.x;
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 4);
    }

    void vectorFrontBackAreLvalues()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                vector<int> xs = {1, 2, 3};
                xs.front() = 10;
                xs.back() = xs.front() + 5;
                return xs[0] + xs[2];
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 25);
    }

    void vectorAssignmentCopies()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                vector<int> a = {1, 2};
                vector<int> b = a;
                b[0] = 9;
                return a[0] + b[0];
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 10);
    }

    void vectorReferenceParameterWritesBack()
    {
        const QString src = QStringLiteral(R"(
            fn void mutate(vector<int>& xs) {
                xs[0] = xs[0] + 5;
                xs.push(8);
            }

            fn int main() {
                vector<int> xs = {1};
                mutate(xs);
                return xs[0] + xs[1];
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 14);
    }

    void buildStringBuiltinWorks()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                str s = build_string("x=", 4, ", ok=", true);
                if (s == "x=4, ok=true") {
                    return 12;
                }
                return 0;
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 12);
    }

    void stringCharConversionsWork()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                vector<char> cs = str_to_chars("az");
                cs[1] = 'b';
                str s = chars_to_str(cs);
                if (s == "ab") {
                    return cs.len() + 20;
                }
                return 0;
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 22);
    }

    void castPipeAndExtendedOperatorsWork()
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
                if (roundtrip == "az") {
                    return (2 ** 3) + (-5 %% 3) + (x <? 10) + (piped >? 4);
                }
                return 0;
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 25);
    }

    void prvalueVectorReceiverAndNumericConversionsWork()
    {
        const QString src = QStringLiteral(R"(
            fn int take_int(int x) {
                return x;
            }

            fn int main() {
                str ans = "abc";
                int len = 5;
                while (str_to_chars(ans).len() < len) {
                    len = len - 10;
                }

                str_to_chars(ans).push('d');
                char front = str_to_chars(ans).front();

                int a = 4.6;
                int b = cast<int>(4.6);
                double d = cast<double>(a);
                int e = take_int(3.9);

                if (front == 'a') {
                    return len + a + b + cast<int>(d) + e + 100;
                }
                return 0;
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 110);
    }

    void sourceLocationBuiltinsReportCallSite()
    {
        const QString src = QStringLiteral(
            "fn int main() {\n"
            "    int line = __LINE__;\n"
            "    int column = __COLUMN__;\n"
            "    str file = __FILE__;\n"
            "    if (file == \"<test>\" && line == 2 && column == 18) {\n"
            "        return line + column;\n"
            "    }\n"
            "    return 0;\n"
            "}\n");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 20);
    }

    void buildStringUsesUserToStr()
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
                if (text == "student=Aya(16)") {
                    return 16;
                }
                return 0;
            }
        )ABEL");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 16);
    }

    void badAnyCastReportsRuntimeError()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                any x = "bad";
                return cast<int>(x);
            }
        )");
        auto result = runSource(src);
        QVERIFY(!result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 1);
    }

    void anyVariadicUserFunctionPacksArgs()
    {
        const QString src = QStringLiteral(R"(
            fn int count(any... args) {
                return args.len();
            }

            fn int main() {
                return count(1, "a", true);
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 3);
    }

    void cStyleForWorks()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                int sum = 0;
                for (int i = 0; i < 5; i = i + 1) {
                    if (i == 3) {
                        continue;
                    }
                    sum = sum + i;
                }
                return sum;
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 7);
    }

    void rangeForMutatesVectorElements()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                vector<int> xs = {1, 2, 3};
                for (x in xs) {
                    x = x + 10;
                }
                return xs[0] + xs[1] + xs[2];
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 36);
    }

    void structCounterWorks()
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
                c.inc();
                return c.get();
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 2);
    }

    void structAssignmentCopies()
    {
        const QString src = QStringLiteral(R"(
            struct Pair {
                int x;
                int y;
            }

            fn int main() {
                Pair a = Pair(1, 2);
                Pair b = a;
                b.x = 10;
                return a.x + b.x + b.y;
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 13);
    }

    void lambdaValueCaptureCopies()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                int x = 1;
                func int() f = lambda [=] int() {
                    return x;
                };
                x = 9;
                return f();
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 1);
    }

    void lambdaReferenceCaptureWritesBack()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                int x = 1;
                func void() g = lambda [&] void() {
                    x = x + 9;
                };
                g();
                return x;
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 10);
    }

    void lambdaMixedCapturesWork()
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
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 4);
    }

    void runtimeErrorReportsUserCallStack()
    {
        const QString src = QStringLiteral(R"(
            fn int inner() {
                return 1 / 0;
            }

            fn int outer() {
                return inner();
            }

            fn int main() {
                return outer();
            }
        )");
        auto result = runSource(src);
        QVERIFY(!result.diagnostics.isEmpty());
        const auto& diagnostic = result.diagnostics.front();
        QCOMPARE(diagnostic.code, QStringLiteral("E0517"));
        QVERIFY(stackHasSymbol(diagnostic, QStringLiteral("fn inner")));
        QVERIFY(stackHasSymbol(diagnostic, QStringLiteral("fn outer")));
        QVERIFY(stackHasSymbol(diagnostic, QStringLiteral("fn main")));
        QVERIFY(stackIndexOf(diagnostic, QStringLiteral("fn inner")) < stackIndexOf(diagnostic, QStringLiteral("fn outer")));
        QVERIFY(stackIndexOf(diagnostic, QStringLiteral("fn outer")) < stackIndexOf(diagnostic, QStringLiteral("fn main")));
        QCOMPARE(diagnostic.primary.file, QStringLiteral("<test>"));
        QVERIFY(diagnostic.primary.startLine > 0);
        QVERIFY(diagnostic.primary.startColumn > 0);
        QCOMPARE(result.exitCode, 1);
    }

    void runtimeErrorReportsLambdaCallStack()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                func int() f = lambda [] int() {
                    return 1 / 0;
                };
                return f();
            }
        )");
        auto result = runSource(src);
        QVERIFY(!result.diagnostics.isEmpty());
        const auto& diagnostic = result.diagnostics.front();
        QCOMPARE(diagnostic.code, QStringLiteral("E0517"));
        QVERIFY(stackHasSymbol(diagnostic, QStringLiteral("lambda")));
        QVERIFY(stackHasSymbol(diagnostic, QStringLiteral("fn main")));
        QVERIFY(stackIndexOf(diagnostic, QStringLiteral("lambda")) < stackIndexOf(diagnostic, QStringLiteral("fn main")));
        QCOMPARE(result.exitCode, 1);
    }

    void runtimeErrorReportsMethodCallStack()
    {
        const QString src = QStringLiteral(R"(
            struct Box {
                int x;

                fn int crash() {
                    return 1 / 0;
                }
            }

            fn int main() {
                Box b = Box(1);
                return b.crash();
            }
        )");
        auto result = runSource(src);
        QVERIFY(!result.diagnostics.isEmpty());
        const auto& diagnostic = result.diagnostics.front();
        QCOMPARE(diagnostic.code, QStringLiteral("E0517"));
        QVERIFY(stackHasSymbol(diagnostic, QStringLiteral("method crash")));
        QVERIFY(stackHasSymbol(diagnostic, QStringLiteral("fn main")));
        QVERIFY(stackIndexOf(diagnostic, QStringLiteral("method crash")) < stackIndexOf(diagnostic, QStringLiteral("fn main")));
        QCOMPARE(result.exitCode, 1);
    }

    void backendCallReportsUnboundBackend()
    {
        const QString src = QStringLiteral(R"(
            backend MathSystem {
                fn int fast_add(int a, int b);
            }

            fn int main() {
                return MathSystem::fast_add(1, 2);
            }
        )");
        auto result = runSource(src);
        QVERIFY(!result.diagnostics.isEmpty());
        const auto& diagnostic = result.diagnostics.front();
        QCOMPARE(diagnostic.code, QStringLiteral("E0607"));
        QVERIFY(stackHasSymbol(diagnostic, QStringLiteral("backend MathSystem::fast_add")));
        QVERIFY(stackHasSymbol(diagnostic, QStringLiteral("fn main")));
        QVERIFY(stackIndexOf(diagnostic, QStringLiteral("backend MathSystem::fast_add")) < stackIndexOf(diagnostic, QStringLiteral("fn main")));
        QCOMPARE(result.exitCode, 1);
    }
};

QTEST_MAIN(AbelInterpreterTests)

#include "test_interpreter.moc"
