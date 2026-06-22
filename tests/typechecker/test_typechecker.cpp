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

private slots:
    void acceptsArithmeticMain()
    {
        auto result = checkSource(QStringLiteral("fn int main() { return 1 + 2 * 3; }"));
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
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

    void rejectsAssigningConstVectorFront()
    {
        auto result = checkSource(QStringLiteral("fn int main() { const vector<int> xs = {1}; xs.front() = 2; return 0; }"));
        QVERIFY(!result.diagnostics.isEmpty());
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
