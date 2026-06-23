#include "abelcore/lexer.h"
#include "abelcore/parser.h"

#include <QtTest/QtTest>

class AbelParserTests final : public QObject {
    Q_OBJECT

private slots:
    void parsesFunctionAndReturn()
    {
        const QString src = QStringLiteral("fn int main() { return 1 + 2; }");
        abel::Lexer lexer;
        auto lexed = lexer.lex(QStringLiteral("<test>"), src);
        QVERIFY2(lexed.diagnostics.isEmpty(), "lexer diagnostics must be empty");

        abel::Parser parser;
        auto parsed = parser.parse(lexed.tokens);
        QVERIFY2(parsed.diagnostics.isEmpty(), "parser diagnostics must be empty");
        QVERIFY(parsed.program != nullptr);
        QCOMPARE(parsed.program->declarations.size(), static_cast<size_t>(1));
    }

    void parsesModuleAndUseDeclarations()
    {
        const QString src = QStringLiteral(R"(
            module app.main;
            use app.math;

            export fn int main() {
                return helper();
            }
        )");
        abel::Lexer lexer;
        auto lexed = lexer.lex(QStringLiteral("<test>"), src);
        QVERIFY(lexed.diagnostics.isEmpty());

        abel::Parser parser;
        auto parsed = parser.parse(lexed.tokens);
        for (const auto& d : parsed.diagnostics)
            qWarning() << d.message;
        QVERIFY(parsed.diagnostics.isEmpty());
        QVERIFY(parsed.program != nullptr);
        QCOMPARE(parsed.program->declarations.size(), static_cast<size_t>(3));
        auto* module = dynamic_cast<abel::ModuleDeclNode*>(parsed.program->declarations[0].get());
        auto* use = dynamic_cast<abel::UseDeclNode*>(parsed.program->declarations[1].get());
        QVERIFY(module != nullptr);
        QVERIFY(use != nullptr);
        QCOMPARE(module->name, QStringLiteral("app.main"));
        QCOMPARE(use->name, QStringLiteral("app.math"));
    }

    void parsesVectorAndBackend()
    {
        const QString src = QStringLiteral(R"(
            backend MathSystem {
                fn int fast_add(int a, int b);
            }

            fn int main() {
                vector<int> xs = {1, 2, 3};
                return MathSystem::fast_add(xs[0], xs[1]);
            }
        )");
        abel::Lexer lexer;
        auto lexed = lexer.lex(QStringLiteral("<test>"), src);
        QVERIFY(lexed.diagnostics.isEmpty());

        abel::Parser parser;
        auto parsed = parser.parse(lexed.tokens);
        for (const auto& d : parsed.diagnostics)
            qWarning() << d.message;
        QVERIFY(parsed.diagnostics.isEmpty());
        QCOMPARE(parsed.program->declarations.size(), static_cast<size_t>(2));
    }

    void validatesVariadicParameterShape()
    {
        auto parse = [](const QString& src) {
            abel::Lexer lexer;
            auto lexed = lexer.lex(QStringLiteral("<test>"), src);
            if (!lexed.diagnostics.isEmpty())
                return lexed.diagnostics;
            abel::Parser parser;
            return parser.parse(lexed.tokens).diagnostics;
        };

        QVERIFY(parse(QStringLiteral("fn int ok(any... args) { return 0; }")).isEmpty());
        QVERIFY(!parse(QStringLiteral("fn int bad(any... args, int x) { return 0; }")).isEmpty());
        QVERIFY(!parse(QStringLiteral("fn int bad(int... xs) { return 0; }")).isEmpty());
    }

    void parsesForLoops()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                int s = 0;
                for (int i = 0; i < 3; i = i + 1) {
                    s = s + i;
                }
                vector<int> xs = {1, 2, 3};
                for (x in xs) {
                    s = s + x;
                }
                return s;
            }
        )");
        abel::Lexer lexer;
        auto lexed = lexer.lex(QStringLiteral("<test>"), src);
        QVERIFY(lexed.diagnostics.isEmpty());

        abel::Parser parser;
        auto parsed = parser.parse(lexed.tokens);
        for (const auto& d : parsed.diagnostics)
            qWarning() << d.message;
        QVERIFY(parsed.diagnostics.isEmpty());
        QCOMPARE(parsed.program->declarations.size(), static_cast<size_t>(1));
    }

    void parsesStructCounter()
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
        abel::Lexer lexer;
        auto lexed = lexer.lex(QStringLiteral("<test>"), src);
        QVERIFY(lexed.diagnostics.isEmpty());

        abel::Parser parser;
        auto parsed = parser.parse(lexed.tokens);
        for (const auto& d : parsed.diagnostics)
            qWarning() << d.message;
        QVERIFY(parsed.diagnostics.isEmpty());
        QCOMPARE(parsed.program->declarations.size(), static_cast<size_t>(2));
    }

    void parsesFuncTypeAndLambda()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                int x = 1;
                int y = 2;
                func int() f = lambda [x, &y] int() {
                    y = y + 1;
                    return x + y;
                };
                return f();
            }
        )");
        abel::Lexer lexer;
        auto lexed = lexer.lex(QStringLiteral("<test>"), src);
        QVERIFY(lexed.diagnostics.isEmpty());

        abel::Parser parser;
        auto parsed = parser.parse(lexed.tokens);
        for (const auto& d : parsed.diagnostics)
            qWarning() << d.message;
        QVERIFY(parsed.diagnostics.isEmpty());
        QCOMPARE(parsed.program->declarations.size(), static_cast<size_t>(1));
    }

    void parsesCastPipeAndExtendedOperators()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                any x = 7;
                int y = cast<int>(x);
                str s = "ab" |> str_to_chars |> chars_to_str;
                return (2 ** 3) + (-5 %% 3) + (y <? 10) + (9 >? 4);
            }
        )");
        abel::Lexer lexer;
        auto lexed = lexer.lex(QStringLiteral("<test>"), src);
        QVERIFY(lexed.diagnostics.isEmpty());

        abel::Parser parser;
        auto parsed = parser.parse(lexed.tokens);
        for (const auto& d : parsed.diagnostics)
            qWarning() << d.message;
        QVERIFY(parsed.diagnostics.isEmpty());
        QCOMPARE(parsed.program->declarations.size(), static_cast<size_t>(1));
    }
};

QTEST_MAIN(AbelParserTests)

#include "test_parser.moc"
