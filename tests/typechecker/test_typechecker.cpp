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
};

QTEST_MAIN(AbelTypeCheckerTests)

#include "test_typechecker.moc"
