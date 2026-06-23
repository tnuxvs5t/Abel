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

    void stringBuiltinMethodsWork()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                str s = build_string("hakurei", " shrine");
                bool ok = s.contains("rei") && !s.contains("moriya") && !s.empty();
                int pos = s.find("shrine");
                int missing = s.find("scarlet");
                str a = s.substr(0, 7);
                str b = s.slice(pos, 6);
                str c = s.replace("shrine", "engine");
                if (ok && pos == 8 && missing == -1 && a == "hakurei" && b == "shrine" && c == "hakurei engine") {
                    return s.len() + a.len() + b.len() + c.len() + pos;
                }
                return 0;
            }
        )");
        auto result = runSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 49);
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
};

QTEST_MAIN(AbelInterpreterTests)

#include "test_interpreter.moc"
