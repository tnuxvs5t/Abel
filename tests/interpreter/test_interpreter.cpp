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
        QCOMPARE(result.diagnostics.front().code, QStringLiteral("E0607"));
        QCOMPARE(result.exitCode, 1);
    }
};

QTEST_MAIN(AbelInterpreterTests)

#include "test_interpreter.moc"
