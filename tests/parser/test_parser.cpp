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
};

QTEST_MAIN(AbelParserTests)

#include "test_parser.moc"
