#include "abelcore/interpreter.h"
#include "abelcore/lexer.h"
#include "abelcore/parser.h"

#include <QTemporaryDir>
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

    void runsFixedWidthIntegerTypes()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                i8 a = 127;
                i8 wrapped = a + 1;
                i16 b = wrapped + 130;
                i32 c = cast<i32>(b);
                i64 d = cast<i64>(c);

                u8 e = 255;
                u8 zero = e + 1;
                u16 f = e + 10;
                u32 g = cast<u32>(f);
                u64 h = cast<u64>(g);

                str text = build_string(wrapped, ":", zero, ":", h);
                if (text == "-128:0:265") {
                    return cast<int>(d + h);
                }
                return 0;
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 267);
    }

    void enumAndTypeAliasRun()
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
                if (Color.Red == 0 && Color.Blue == 2) {
                    return c + xs[1];
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

    void constReferenceReadsAndBlocksMutation()
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
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 13);

        auto mutatedParam = runSource(QStringLiteral(R"(
            fn void bad(const int& x) {
                x = 1;
            }

            fn int main() {
                int a = 0;
                bad(a);
                return 0;
            }
        )"));
        QVERIFY(!mutatedParam.diagnostics.isEmpty());

        auto mutableRefToConst = runSource(QStringLiteral(R"(
            fn void inc(int& x) {
                x = x + 1;
            }

            fn int main() {
                const int a = 1;
                inc(a);
                return 0;
            }
        )"));
        QVERIFY(!mutableRefToConst.diagnostics.isEmpty());
    }

    void readonlyLocationsBlockMutationAtRuntime()
    {
        auto constIndex = runSource(QStringLiteral("fn int main() { const vector<int> xs = {1}; xs[0] = 2; return 0; }"));
        QVERIFY(!constIndex.diagnostics.isEmpty());

        auto constFront = runSource(QStringLiteral("fn int main() { const vector<int> xs = {1}; xs.front() = 2; return 0; }"));
        QVERIFY(!constFront.diagnostics.isEmpty());

        auto constPush = runSource(QStringLiteral("fn int main() { const vector<int> xs = {1}; xs.push(2); return 0; }"));
        QVERIFY(!constPush.diagnostics.isEmpty());

        auto constRange = runSource(QStringLiteral(R"(
            fn int main() {
                const vector<int> xs = {1, 2};
                for (x in xs) {
                    x = x + 1;
                }
                return 0;
            }
        )"));
        QVERIFY(!constRange.diagnostics.isEmpty());

        auto constField = runSource(QStringLiteral(R"(
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

        auto constReceiver = runSource(QStringLiteral(R"(
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

    void vectorInsertEraseFindSortWork()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                vector<int> xs = {3, 1, 2};
                xs.insert(2, 2);
                int missing = xs.find(9);
                bool containsTwo = xs.contains(2);
                int copies = xs.count(2);
                vector<int> mid = xs.slice(1, 2);
                xs.extend(mid);
                xs.sort();
                int lb = xs.lower_bound(2);
                int ub = xs.upper_bound(2);
                bool has = xs.binary_search(2);
                bool miss = xs.binary_search(9);
                xs.unique();
                xs.reverse();
                int removed = xs.erase(1);

                vector<str> names = {"moriya", "hakurei"};
                names.sort();

                if (names[0] == "hakurei" && has && !miss && containsTwo
                    && copies == 2 && mid.len() == 2) {
                    return missing + lb + ub + removed + xs[0] * 10 + xs[1] + copies + mid[0];
                }
                return 0;
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 42);
    }

    void mathBuiltinsWork()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                int a = abs(-7);
                double score = sqrt(9)
                    + floor(3.9)
                    + ceil(3.1)
                    + round(3.5)
                    + trunc(3.9)
                    + pow(2, 3)
                    + sin(0)
                    + cos(0)
                    + tan(0)
                    + asin(0)
                    + acos(1)
                    + atan(0)
                    + atan2(0, 1)
                    + exp(0)
                    + log(1)
                    + log10(100)
                    + min(4, 5.5)
                    + max(2, 3)
                    + clamp(10, 0, 7)
                    + (4 |> max(9))
                    + (3 |> clamp(1, 9));
                int divisors = gcd(54, 24) + lcm(6, 8) + (54 |> gcd(24));
                return a + cast<int>(score) + divisors;
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 98);
    }

    void mathClampRejectsBadBoundsAtRuntime()
    {
        auto result = runSource(QStringLiteral(R"(
            fn int main() {
                int x = clamp(1, 9, 2);
                return x;
            }
        )"));
        QVERIFY(!result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 1);
        QVERIFY(result.diagnostics.front().message.contains(QStringLiteral("clamp lower bound")));
    }

    void vectorInsertEraseRejectOutOfRangeAtRuntime()
    {
        auto badErase = runSource(QStringLiteral(R"(
            fn int main() {
                vector<int> xs = {1};
                xs.erase(9);
                return 0;
            }
        )"));
        QVERIFY(!badErase.diagnostics.isEmpty());
        QCOMPARE(badErase.exitCode, 1);
        QVERIFY(badErase.diagnostics.front().message.contains(QStringLiteral("vector.erase index out of range")));

        auto badInsert = runSource(QStringLiteral(R"(
            fn int main() {
                vector<int> xs = {1};
                xs.insert(-1, 2);
                return 0;
            }
        )"));
        QVERIFY(!badInsert.diagnostics.isEmpty());
        QCOMPARE(badInsert.exitCode, 1);
        QVERIFY(badInsert.diagnostics.front().message.contains(QStringLiteral("vector.insert index out of range")));
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

    void charAndAnyBuiltinsWork()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                char a = 'a';
                int code = char_code(a);
                char from = char_from_code(code);
                any x = 7;
                any text = "kappa";
                any chars = str_to_chars("ab");
                bool ok = from == a
                    && char_is_lower(a)
                    && char_is_upper(char_upper(a))
                    && char_lower('A') == a
                    && char_is_digit('7')
                    && char_is_letter('河')
                    && char_is_alnum('8')
                    && char_is_space(' ')
                    && char_to_str(from) == "a"
                    && any_type(x) == "i32"
                    && any_is(x, "integer")
                    && any_is_int(x)
                    && !any_is_double(x)
                    && any_is_str(text)
                    && any_is_vector(chars)
                    && (a |> char_upper |> char_is_upper)
                    && (x |> any_is_int)
                    && (x |> any_is("i32"));
                if (ok) {
                    return code;
                }
                return 0;
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 97);
    }

    void charFromCodeRejectsBadRangeAtRuntime()
    {
        auto result = runSource(QStringLiteral(R"(
            fn int main() {
                char c = char_from_code(70000);
                return char_code(c);
            }
        )"));
        QVERIFY(!result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 1);
        QVERIFY(result.diagnostics.front().message.contains(QStringLiteral("char_from_code expects codepoint")));
    }

    void stringBuiltinMethodsWork()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                str s = build_string("hakurei", " shrine");
                bool ok = s.contains("rei") && !s.contains("moriya") && !s.empty()
                    && s.starts_with("hak") && s.ends_with("shrine");
                int pos = s.find("shrine");
                int missing = s.find("scarlet");
                str a = s.substr(0, 7);
                str b = s.slice(pos, 6);
                str c = s.replace("shrine", "engine");
                str t = "  Aya  ".trim().lower().upper();
                vector<str> parts = s.split(" ");
                str joined = "/".join(parts);
                int parsed = "123".parse_int();
                long parsedLong = "5000".parse_long();
                double parsedDouble = "2.5".parse_double();
                bool parsedBool = "true".parse_bool();
                if (ok && pos == 8 && missing == -1 && a == "hakurei" && b == "shrine"
                    && c == "hakurei engine" && t == "AYA"
                    && parts.len() == 2 && parts[0] == "hakurei" && parts[1] == "shrine"
                    && joined == "hakurei/shrine" && parsedLong == 5000
                    && parsedDouble == 2.5 && parsedBool) {
                    return s.len() + a.len() + b.len() + c.len() + t.len()
                        + parts.len() + pos + joined.len() + parsed + cast<int>(parsedDouble);
                }
                return 0;
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 193);
    }

    void stringSliceRejectsNegativeBoundsAtRuntime()
    {
        auto result = runSource(QStringLiteral(R"(
            fn int main() {
                return "abc".slice(-1, 1).len();
            }
        )"));
        QVERIFY(!result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 1);
        QVERIFY(result.diagnostics.front().message.contains(QStringLiteral("non-negative")));
    }

    void stringParseRejectsBadInputAtRuntime()
    {
        auto badInt = runSource(QStringLiteral(R"(
            fn int main() {
                return "abc".parse_int();
            }
        )"));
        QVERIFY(!badInt.diagnostics.isEmpty());
        QCOMPARE(badInt.exitCode, 1);
        QVERIFY(badInt.diagnostics.front().message.contains(QStringLiteral("parse_int")));

        auto badBool = runSource(QStringLiteral(R"(
            fn int main() {
                bool ok = "maybe".parse_bool();
                return ok;
            }
        )"));
        QVERIFY(!badBool.diagnostics.isEmpty());
        QCOMPARE(badBool.exitCode, 1);
        QVERIFY(badBool.diagnostics.front().message.contains(QStringLiteral("parse_bool")));
    }

    void fileAndPathBuiltinsWork()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString dir = temp.path() + QStringLiteral("/nested/io");
        const QString file = dir + QStringLiteral("/story.txt");
        const QString src = QStringLiteral(R"(
            fn int main() {
                str dir = "%1";
                str file = "%2";
                mkdirs(dir);
                write_text(file, "alpha\nbeta");
                append_text(file, "\ngamma");
                str text = read_text(file);
                vector<str> lines = {"hakurei", "kappa"};
                write_lines(file, lines);
                vector<str> read = read_lines(file);
                str copy = path_join(dir, "copy.txt");
                str moved = path_join(dir, "moved.txt");
                copy_file(file, copy);
                bool copied = path_is_file(copy);
                copy |> move_path(moved);
                bool movedOk = path_is_file(moved) && !path_exists(copy);
                remove_path(moved);
                bool removed = !path_exists(moved);
                str parent = path_dirname(file);
                str base = path_basename(file);
                str ext = path_ext(file);
                str abs = path_absolute(file);
                str clean = path_clean(build_string(dir, "/../io/story.txt"));
                str cwd = current_dir();
                bool envOk = env_exists("PATH") && env_get("PATH").len() > 0;
                bool ok = path_exists(file) && path_is_file(file) && path_is_dir(dir);
                bool pipeOk = file |> path_exists;
                file |> write_text("delta");
                str final = read_text(file);
                bool pathOk = parent == dir && base == "story.txt" && ext == "txt"
                    && abs.len() > 0 && clean.ends_with("io/story.txt") && cwd.len() > 0;
                if (ok && pipeOk && copied && movedOk && removed && pathOk && envOk && final == "delta") {
                    return text.len() + read.len() + read[0].len() + read[1].len() + 2;
                }
                return 0;
            }
        )").arg(dir, file);
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 32);
    }

    void fileBuiltinReportsRuntimeErrors()
    {
        auto result = runSource(QStringLiteral(R"(
            fn int main() {
                str text = read_text("/definitely/missing/abel-file.txt");
                return text.len();
            }
        )"));
        QVERIFY(!result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 1);
        QVERIFY(result.diagnostics.front().message.contains(QStringLiteral("read_text cannot open")));
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

    void privateStructInternalsWorkAtRuntime()
    {
        const QString src = QStringLiteral(R"(
            struct Vault {
            private:
                int secret;

                fn int leak() {
                    return secret;
                }

            public:
                init(int x) {
                    secret = x;
                }

                fn int get() {
                    Vault other = Vault(8);
                    return secret + other.leak();
                }
            }

            fn int main() {
                Vault v = Vault(9);
                return v.get();
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 17);
    }

    void rejectsExternalPrivateStructMembersAtRuntime()
    {
        auto privateField = runSource(QStringLiteral(R"(
            struct Vault {
            private:
                int secret;
            public:
                init(int x) {
                    secret = x;
                }
            }

            fn int main() {
                Vault v = Vault(7);
                return v.secret;
            }
        )"));
        QVERIFY(!privateField.diagnostics.isEmpty());

        auto privateMethod = runSource(QStringLiteral(R"(
            struct Vault {
            private:
                int secret;

                fn int leak() {
                    return secret;
                }

            public:
                init(int x) {
                    secret = x;
                }
            }

            fn int main() {
                Vault v = Vault(7);
                return v.leak();
            }
        )"));
        QVERIFY(!privateMethod.diagnostics.isEmpty());

        auto privateConstructor = runSource(QStringLiteral(R"(
            struct Vault {
            private:
                init(int x) {
                }

            public:
                fn int get() {
                    return 0;
                }
            }

            fn int main() {
                Vault v = Vault(7);
                return v.get();
            }
        )"));
        QVERIFY(!privateConstructor.diagnostics.isEmpty());

        auto privatePositionalField = runSource(QStringLiteral(R"(
            struct Box {
            private:
                int x;

            public:
                fn int get() {
                    return x;
                }
            }

            fn int main() {
                Box b = Box(1);
                return b.get();
            }
        )"));
        QVERIFY(!privatePositionalField.diagnostics.isEmpty());

        auto privateDefaultResize = runSource(QStringLiteral(R"(
            struct Hidden {
                int x;

            private:
                init() {
                    x = 1;
                }

            public:
                fn int get() {
                    return x;
                }
            }

            fn int main() {
                vector<Hidden> xs;
                xs.resize(1);
                return 0;
            }
        )"));
        QVERIFY(!privateDefaultResize.diagnostics.isEmpty());
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
        QVERIFY(diagnostic.primary.sourceLine.contains(QStringLiteral("return 1 / 0;")));
        bool sawInnerCallLine = false;
        bool sawOuterCallLine = false;
        bool sawMainLine = false;
        for (const auto& frame : diagnostic.stackTrace) {
            if (frame.symbol == QStringLiteral("fn inner"))
                sawInnerCallLine = frame.callSite.sourceLine.contains(QStringLiteral("return inner();"));
            if (frame.symbol == QStringLiteral("fn outer"))
                sawOuterCallLine = frame.callSite.sourceLine.contains(QStringLiteral("return outer();"));
            if (frame.symbol == QStringLiteral("fn main"))
                sawMainLine = frame.callSite.sourceLine.contains(QStringLiteral("fn int main()"));
        }
        QVERIFY(sawInnerCallLine);
        QVERIFY(sawOuterCallLine);
        QVERIFY(sawMainLine);
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
        QVERIFY(diagnostic.primary.sourceLine.contains(QStringLiteral("return 1 / 0;")));
        bool sawLambdaCallLine = false;
        for (const auto& frame : diagnostic.stackTrace) {
            if (frame.symbol == QStringLiteral("lambda"))
                sawLambdaCallLine = frame.callSite.sourceLine.contains(QStringLiteral("return f();"));
        }
        QVERIFY(sawLambdaCallLine);
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
        QVERIFY(diagnostic.primary.sourceLine.contains(QStringLiteral("return 1 / 0;")));
        bool sawMethodCallLine = false;
        for (const auto& frame : diagnostic.stackTrace) {
            if (frame.symbol == QStringLiteral("method crash"))
                sawMethodCallLine = frame.callSite.sourceLine.contains(QStringLiteral("return b.crash();"));
        }
        QVERIFY(sawMethodCallLine);
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
        QVERIFY(diagnostic.primary.sourceLine.contains(QStringLiteral("return MathSystem::fast_add(1, 2);")));
        bool sawBackendCallLine = false;
        for (const auto& frame : diagnostic.stackTrace) {
            if (frame.symbol == QStringLiteral("backend MathSystem::fast_add"))
                sawBackendCallLine = frame.callSite.sourceLine.contains(QStringLiteral("return MathSystem::fast_add(1, 2);"));
        }
        QVERIFY(sawBackendCallLine);
        QCOMPARE(result.exitCode, 1);
    }

    void runtimeConversionErrorsUseArgumentAndReturnSourceLines()
    {
        const QString src = QStringLiteral(R"(
            fn int take(int x) {
                return x;
            }

            fn int bad_return() {
                return "bad";
            }

            fn int main() {
                return take("bad") + bad_return();
            }
        )");
        auto argResult = runSource(src);
        QVERIFY(!argResult.diagnostics.isEmpty());
        const auto& argDiagnostic = argResult.diagnostics.front();
        QCOMPARE(argDiagnostic.code, QStringLiteral("E0531"));
        QVERIFY(argDiagnostic.primary.sourceLine.contains(QStringLiteral("return take(\"bad\") + bad_return();")));
        QVERIFY(stackHasSymbol(argDiagnostic, QStringLiteral("fn main")));

        const QString returnSrc = QStringLiteral(R"(
            fn int bad_return() {
                return "bad";
            }

            fn int main() {
                return bad_return();
            }
        )");
        auto returnResult = runSource(returnSrc);
        QVERIFY(!returnResult.diagnostics.isEmpty());
        const auto& returnDiagnostic = returnResult.diagnostics.front();
        QCOMPARE(returnDiagnostic.code, QStringLiteral("E0531"));
        QVERIFY(returnDiagnostic.primary.sourceLine.contains(QStringLiteral("return \"bad\";")));
        QVERIFY(stackHasSymbol(returnDiagnostic, QStringLiteral("fn bad_return")));
        QVERIFY(stackHasSymbol(returnDiagnostic, QStringLiteral("fn main")));
    }

    void runtimeConversionErrorsUseLambdaMethodAndBackendSourceLines()
    {
        const QString lambdaSrc = QStringLiteral(R"(
            fn int main() {
                func int() f = lambda [] int() {
                    return "bad";
                };
                return f();
            }
        )");
        auto lambdaResult = runSource(lambdaSrc);
        QVERIFY(!lambdaResult.diagnostics.isEmpty());
        const auto& lambdaDiagnostic = lambdaResult.diagnostics.front();
        QCOMPARE(lambdaDiagnostic.code, QStringLiteral("E0531"));
        QVERIFY(lambdaDiagnostic.primary.sourceLine.contains(QStringLiteral("return \"bad\";")));
        QVERIFY(stackHasSymbol(lambdaDiagnostic, QStringLiteral("lambda")));

        const QString methodSrc = QStringLiteral(R"(
            struct Box {
                fn int set(int x) {
                    return x;
                }
            }

            fn int main() {
                Box b = Box();
                return b.set("bad");
            }
        )");
        auto methodResult = runSource(methodSrc);
        QVERIFY(!methodResult.diagnostics.isEmpty());
        const auto& methodDiagnostic = methodResult.diagnostics.front();
        QCOMPARE(methodDiagnostic.code, QStringLiteral("E0531"));
        QVERIFY(methodDiagnostic.primary.sourceLine.contains(QStringLiteral("return b.set(\"bad\");")));
        QVERIFY(stackHasSymbol(methodDiagnostic, QStringLiteral("fn main")));

        const QString backendSrc = QStringLiteral(R"(
            backend MathSystem {
                fn int accept(int x);
            }

            fn int main() {
                return MathSystem::accept("bad");
            }
        )");
        auto backendResult = runSource(backendSrc);
        QVERIFY(!backendResult.diagnostics.isEmpty());
        const auto& backendDiagnostic = backendResult.diagnostics.front();
        QCOMPARE(backendDiagnostic.code, QStringLiteral("E0531"));
        QVERIFY(backendDiagnostic.primary.sourceLine.contains(QStringLiteral("return MathSystem::accept(\"bad\");")));
        QVERIFY(stackHasSymbol(backendDiagnostic, QStringLiteral("fn main")));
    }

    void debugBreakReportsStackAndSourceLine()
    {
        const QString src = QStringLiteral(R"(
            fn void stop() {
                debug_break();
            }

            fn int main() {
                stop();
                return 0;
            }
        )");
        auto result = runSource(src);
        QVERIFY(!result.diagnostics.isEmpty());
        const auto& diagnostic = result.diagnostics.front();
        QCOMPARE(diagnostic.code, QStringLiteral("E0596"));
        QVERIFY(diagnostic.primary.sourceLine.contains(QStringLiteral("debug_break();")));
        QVERIFY(stackHasSymbol(diagnostic, QStringLiteral("fn stop")));
        QVERIFY(stackHasSymbol(diagnostic, QStringLiteral("fn main")));
        bool sawStopCallLine = false;
        for (const auto& frame : diagnostic.stackTrace) {
            if (frame.symbol == QStringLiteral("fn stop"))
                sawStopCallLine = frame.callSite.sourceLine.contains(QStringLiteral("stop();"));
        }
        QVERIFY(sawStopCallLine);
        QCOMPARE(result.exitCode, 1);
    }

    void debugAssertTrueContinues()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                debug_assert(true, "ok");
                return 7;
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 7);
    }

    void debugAssertFalseReportsMessageStackAndSourceLine()
    {
        const QString src = QStringLiteral(R"(
            fn void check() {
                debug_assert(false, "x=", 4);
            }

            fn int main() {
                check();
                return 0;
            }
        )");
        auto result = runSource(src);
        QVERIFY(!result.diagnostics.isEmpty());
        const auto& diagnostic = result.diagnostics.front();
        QCOMPARE(diagnostic.code, QStringLiteral("E0598"));
        QVERIFY(diagnostic.message.contains(QStringLiteral("x=4")));
        QVERIFY(diagnostic.primary.sourceLine.contains(QStringLiteral("debug_assert(false")));
        QVERIFY(stackHasSymbol(diagnostic, QStringLiteral("fn check")));
        QVERIFY(stackHasSymbol(diagnostic, QStringLiteral("fn main")));
        QCOMPARE(result.exitCode, 1);
    }

    void testAssertionsPassWhenSatisfied()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                test_assert(true, "ok");
                test_eq(4, 4.0, "numeric");
                test_ne("a", "b");
                test_close(3.14159, 3.14, 0.01, "close");
                return 7;
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 7);
    }

    void testEqFailureReportsMessageStackAndSourceLine()
    {
        const QString src = QStringLiteral(R"(
            fn void check() {
                test_eq("actual", "expected", " label=", 9);
            }

            fn int main() {
                check();
                return 0;
            }
        )");
        auto result = runSource(src);
        QVERIFY(!result.diagnostics.isEmpty());
        const auto& diagnostic = result.diagnostics.front();
        QCOMPARE(diagnostic.code, QStringLiteral("E0599"));
        QVERIFY(diagnostic.message.contains(QStringLiteral("test_eq failed")));
        QVERIFY(diagnostic.message.contains(QStringLiteral("expected")));
        QVERIFY(diagnostic.message.contains(QStringLiteral("actual")));
        QVERIFY(diagnostic.message.contains(QStringLiteral("label=9")));
        QVERIFY(diagnostic.primary.sourceLine.contains(QStringLiteral("test_eq(")));
        QVERIFY(stackHasSymbol(diagnostic, QStringLiteral("fn check")));
        QVERIFY(stackHasSymbol(diagnostic, QStringLiteral("fn main")));
        QCOMPARE(result.exitCode, 1);
    }

    void testCloseFailureReportsMessageStackAndSourceLine()
    {
        const QString src = QStringLiteral(R"(
            fn void check() {
                test_close(3.2, 3.0, 0.01, " label=", 5);
            }

            fn int main() {
                check();
                return 0;
            }
        )");
        auto result = runSource(src);
        QVERIFY(!result.diagnostics.isEmpty());
        const auto& diagnostic = result.diagnostics.front();
        QCOMPARE(diagnostic.code, QStringLiteral("E0599"));
        QVERIFY(diagnostic.message.contains(QStringLiteral("test_close failed")));
        QVERIFY(diagnostic.message.contains(QStringLiteral("label=5")));
        QVERIFY(diagnostic.primary.sourceLine.contains(QStringLiteral("test_close(")));
        QVERIFY(stackHasSymbol(diagnostic, QStringLiteral("fn check")));
        QVERIFY(stackHasSymbol(diagnostic, QStringLiteral("fn main")));
        QCOMPARE(result.exitCode, 1);
    }

    void runsOrdinaryFunctionOverloads()
    {
        const QString src = QStringLiteral(R"(
            fn int pick(int x) {
                return x + 10;
            }

            fn int pick(str s) {
                return s.len();
            }

            fn int pick(double x) {
                return cast<int>(x) + 100;
            }

            fn int bump(int& x) {
                x = x + 1;
                return x;
            }

            fn int twice(int x) {
                return x * 2;
            }

            fn str twice(str s) {
                return s + s;
            }

            fn int main() {
                int x = 5;
                int direct = pick(1) + pick("abc") + pick(2.5);
                int refResult = bump(x);
                int piped = (3 |> twice) + ("ab" |> twice).len();
                return direct + refResult + x + piped;
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 138);
    }

    void runsStructConstructorAndMethodOverloads()
    {
        const QString src = QStringLiteral(R"(
            fn int next(int& g) {
                g = g + 1;
                return g;
            }

            struct Box {
                int x;

                init(int v) {
                    x = v;
                }

                init(str s) {
                    x = s.len();
                }

                fn int get() {
                    return x;
                }

                fn int get(int add) {
                    return x + add;
                }

                fn int bump(int& y) {
                    y = y + 1;
                    return y;
                }
            }

            fn int main() {
                Box a = Box(5);
                Box b = Box("abcd");
                int g = 0;
                Box c = Box(next(g));
                int y = 10;
                return a.get() + b.get(3) + a.bump(y) + y + c.get() + g;
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 36);
    }

    void runsUserBinaryOperators()
    {
        const QString src = QStringLiteral(R"(
            struct Point {
                int x;
                int y;
            }

            struct Size {
                int w;
                int h;
            }

            fn Point operator +(Point a, Point b) {
                return Point(a.x + b.x, a.y + b.y);
            }

            fn Size operator +(Size a, Size b) {
                return Size(a.w + b.w, a.h + b.h);
            }

            fn bool operator ==(Point a, Point b) {
                return a.x == b.x && a.y == b.y;
            }

            fn int main() {
                Point a = Point(1, 2);
                Point b = Point(3, 4);
                Point c = a + b;
                Point d = c + Point(1, 2);
                if (d == Point(5, 8)) {
                    Size s = Size(5, 6) + Size(7, 8);
                    return d.x + d.y + s.w + s.h;
                }
                return 0;
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 39);
    }
};

QTEST_MAIN(AbelInterpreterTests)

#include "test_interpreter.moc"
