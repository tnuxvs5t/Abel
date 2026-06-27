#include "abelcore/lexer.h"
#include "abelcore/parser.h"
#include "abelcore/typechecker.h"

#include <QtTest/QtTest>

class AbelTypeCheckerTests final : public QObject {
    Q_OBJECT

private:
    static void tagInlineModules(abel::ProgramNode& program)
    {
        QString moduleName;
        QList<QString> importedModules;
        QHash<QString, QString> importedModuleAliases;
        for (const auto& decl : program.declarations) {
            if (auto* module = dynamic_cast<abel::ModuleDeclNode*>(decl.get())) {
                moduleName = module->name;
                importedModules.clear();
                importedModuleAliases.clear();
            } else if (auto* use = dynamic_cast<abel::UseDeclNode*>(decl.get())) {
                if (!importedModules.contains(use->name))
                    importedModules.push_back(use->name);
                if (!use->alias.isEmpty())
                    importedModuleAliases.insert(use->alias, use->name);
            }
            decl->moduleName = moduleName;
            decl->importedModules = importedModules;
            decl->importedModuleAliases = importedModuleAliases;
        }
    }

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

        tagInlineModules(*parsed.program);
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

    void acceptsOrdinaryFunctionOverloads()
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

            fn int main() {
                int x = 5;
                return pick(1) + pick("abc") + pick(2.5) + bump(x);
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void acceptsStructuredOrdinaryFunctionCalls()
    {
        const QString src = QStringLiteral(R"(
            fn int inc(int x, int by = 1) {
                return x + by;
            }

            fn int mix(int a, int b, int c = 30) {
                return a + b + c;
            }

            fn int sum(any... xs) {
                return xs.len();
            }

            fn int main() {
                vector<any> tail = {2, "x", true};
                int a = inc(1);
                int b = inc(x: 1, by: 2);
                int c = mix(1, c: 3, b: 2);
                int d = sum(1, "x", true);
                int e = sum(1, ...tail);
                int f = 3 |> inc(x: _, by: 4);
                int g = 3 |> inc(by: 4);
                int h = 3 |> inc;
                int i = tail |> sum("head", ..._);
                str s = build_string("prefix=", ...tail);
                println("debug=", ...tail);
                return a + b + c + d + e + f + g + h + i + s.len();
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void acceptsStructuredConstructorMethodAndBackendCalls()
    {
        const QString src = QStringLiteral(R"(
            backend MathSystem {
                fn int fast_add(int a, int b = 1);
                fn int count(any... xs);
                fn void touch(int& x, int by = 2);
            }

            struct Point {
                int x;
                int y;

                init(int x, int y = 0) {
                    this.x = x;
                    this.y = y;
                }

                const fn int sum(int add = 0) {
                    return x + y + add;
                }

                fn int count(any... xs) {
                    return xs.len();
                }
            }

            fn int main() {
                vector<any> tail = {1, "x", true};
                Point p = Point(x: 3);
                Point q = Point(y: 4, x: 2);
                Point r = 3 |> Point(x: _, y: 4);
                int out = 1;
                MathSystem::touch(x: out);
                out |> MathSystem::touch(x: _, by: 3);
                return p.sum(add: 5)
                    + q.count("prefix", ...tail)
                    + r.sum()
                    + MathSystem::fast_add(b: 2, a: 1)
                    + (3 |> MathSystem::fast_add(a: _, b: 4))
                    + MathSystem::count("prefix", ...tail)
                    + (tail |> MathSystem::count("prefix", ..._))
                    + out;
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void acceptsModuleQualifiedStructuredPipeCalls()
    {
        const QString src = QStringLiteral(R"(
            module lib.math;

            export fn int scale(int x, int by = 2) {
                return x * by;
            }

            export fn int count(any... xs) {
                return xs.len();
            }

            module app.main;

            use lib.math;

            fn int main() {
                vector<any> tail = {"A", 7, true};
                int a = lib.math::scale(x: 1);
                int b = 3 |> lib.math::scale(x: _, by: 4);
                int c = tail |> lib.math::count("prefix", ..._);
                return a + b + c;
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void rejectsBadStructuredOrdinaryFunctionCalls()
    {
        auto positionalAfterNamed = checkSource(QStringLiteral(R"(
            fn int add(int a, int b) {
                return a + b;
            }

            fn int main() {
                return add(a: 1, 2);
            }
        )"));
        QVERIFY(countMessagesContaining(positionalAfterNamed, QStringLiteral("positional argument cannot appear after named")) >= 1);

        auto duplicateNamed = checkSource(QStringLiteral(R"(
            fn int add(int a, int b) {
                return a + b;
            }

            fn int main() {
                return add(a: 1, a: 2);
            }
        )"));
        QVERIFY(countMessagesContaining(duplicateNamed, QStringLiteral("duplicate named argument")) >= 1);

        auto unknownNamed = checkSource(QStringLiteral(R"(
            fn int add(int a, int b) {
                return a + b;
            }

            fn int main() {
                return add(a: 1, z: 2);
            }
        )"));
        QVERIFY(countMessagesContaining(unknownNamed, QStringLiteral("unknown parameter name")) >= 1);

        auto missingRequired = checkSource(QStringLiteral(R"(
            fn int add(int a, int b) {
                return a + b;
            }

            fn int main() {
                return add(a: 1);
            }
        )"));
        QVERIFY(countMessagesContaining(missingRequired, QStringLiteral("missing argument for parameter 'b'")) >= 1);

        auto badDefault = checkSource(QStringLiteral(R"(
            fn int inc(int x, int by = "bad") {
                return x + by;
            }

            fn int main() {
                return inc(1);
            }
        )"));
        QVERIFY(countMessagesContaining(badDefault, QStringLiteral("default value for parameter 'by'")) >= 1);

        auto functionValueNamed = checkSource(QStringLiteral(R"(
            fn int main() {
                func int(int, int) f = lambda [] int(int a, int b) {
                    return a + b;
                };
                return f(a: 1, b: 2);
            }
        )"));
        QVERIFY(countMessagesContaining(functionValueNamed, QStringLiteral("function value calls only accept positional")) >= 1);

        auto spreadIntoFixed = checkSource(QStringLiteral(R"(
            fn int add(int a, int b) {
                return a + b;
            }

            fn int main() {
                vector<any> xs = {1, 2};
                return add(...xs);
            }
        )"));
        QVERIFY(countMessagesContaining(spreadIntoFixed, QStringLiteral("spread argument can only expand into an any... tail")) >= 1);

        auto spreadNonAnyVector = checkSource(QStringLiteral(R"(
            fn int count(any... xs) {
                return xs.len();
            }

            fn int main() {
                vector<int> xs = {1, 2};
                return count(...xs);
            }
        )"));
        QVERIFY(countMessagesContaining(spreadNonAnyVector, QStringLiteral("spread argument expects vector<any>")) >= 1);

        auto functionValueNamedPipe = checkSource(QStringLiteral(R"(
            fn int main() {
                func int(int, int) f = lambda [] int(int a, int b) {
                    return a + b;
                };
                return 1 |> f(a: _, b: 2);
            }
        )"));
        QVERIFY(countMessagesContaining(functionValueNamedPipe, QStringLiteral("function value calls only accept positional")) >= 1);

        auto builtinMethodNamed = checkSource(QStringLiteral(R"(
            fn int main() {
                bool ok = "abel".contains(needle: "be");
                return 0;
            }
        )"));
        QVERIFY(countMessagesContaining(builtinMethodNamed,
                                        QStringLiteral("builtin method calls do not support named")) >= 1);

        auto backendDuplicateMutableRefPipe = checkSource(QStringLiteral(R"(
            backend MathSystem {
                fn void two_refs(int& a, int& b);
            }

            fn int main() {
                int x = 1;
                x |> MathSystem::two_refs(_, _);
                return x;
            }
        )"));
        QVERIFY(countMessagesContaining(backendDuplicateMutableRefPipe,
                                        QStringLiteral("multiple mutable reference parameters")) >= 1);
    }

    void rejectsBadStructuredConstructorMethodAndBackendCalls()
    {
        auto badConstructorName = checkSource(QStringLiteral(R"(
            struct Point {
                int x;
                int y;

                init(int x, int y = 0) {
                    this.x = x;
                    this.y = y;
                }
            }

            fn int main() {
                Point p = Point(z: 1);
                return p.x;
            }
        )"));
        QVERIFY(countMessagesContaining(badConstructorName, QStringLiteral("unknown parameter name")) >= 1);

        auto badMethodName = checkSource(QStringLiteral(R"(
            struct Point {
                int x;

                init(int x) {
                    this.x = x;
                }

                fn int get(int add = 0) {
                    return x + add;
                }
            }

            fn int main() {
                Point p = Point(1);
                return p.get(z: 2);
            }
        )"));
        QVERIFY(countMessagesContaining(badMethodName, QStringLiteral("unknown parameter name")) >= 1);

        auto backendSpreadFixed = checkSource(QStringLiteral(R"(
            backend MathSystem {
                fn int fast_add(int a, int b);
            }

            fn int main() {
                vector<any> xs = {1, 2};
                return MathSystem::fast_add(...xs);
            }
        )"));
        QVERIFY(countMessagesContaining(backendSpreadFixed, QStringLiteral("spread argument can only expand into an any... tail")) >= 1);

        auto backendSpreadNonAnyVector = checkSource(QStringLiteral(R"(
            backend MathSystem {
                fn int count(any... xs);
            }

            fn int main() {
                vector<int> xs = {1, 2};
                return MathSystem::count(...xs);
            }
        )"));
        QVERIFY(countMessagesContaining(backendSpreadNonAnyVector, QStringLiteral("spread argument expects vector<any>")) >= 1);

        auto constructorDuplicateMutableRefPipe = checkSource(QStringLiteral(R"(
            struct RefBox {
                int x;

                init(int& a, int& b) {
                    x = a + b;
                }
            }

            fn int main() {
                int x = 1;
                RefBox b = x |> RefBox(_, _);
                return b.x;
            }
        )"));
        QVERIFY(countMessagesContaining(constructorDuplicateMutableRefPipe,
                                        QStringLiteral("multiple mutable reference parameters")) >= 1);
    }

    void rejectsBadOrdinaryFunctionOverloads()
    {
        auto duplicateSignature = checkSource(QStringLiteral(R"(
            fn int f(int x) {
                return x;
            }

            fn int f(int y) {
                return y;
            }

            fn int main() {
                return 0;
            }
        )"));
        QVERIFY(!duplicateSignature.diagnostics.isEmpty());

        auto noMatch = checkSource(QStringLiteral(R"(
            fn int f(int x) {
                return x;
            }

            fn int f(double x) {
                return cast<int>(x);
            }

            fn int main() {
                return f("bad");
            }
        )"));
        QVERIFY(!noMatch.diagnostics.isEmpty());
        QVERIFY(countMessagesContaining(noMatch, QStringLiteral("no matching function 'f' overload")) >= 1);

        auto ambiguous = checkSource(QStringLiteral(R"(
            fn int f(any a, int b) {
                return 1;
            }

            fn int f(int a, any b) {
                return 2;
            }

            fn int main() {
                return f(1, 2);
            }
        )"));
        QVERIFY(!ambiguous.diagnostics.isEmpty());
        QVERIFY(countMessagesContaining(ambiguous, QStringLiteral("function 'f' overload is ambiguous")) >= 1);
    }

    void acceptsStructConstructorAndMethodOverloads()
    {
        const QString src = QStringLiteral(R"(
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
                int y = 10;
                return a.get() + b.get(3) + a.bump(y) + y;
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void rejectsBadStructConstructorAndMethodOverloads()
    {
        auto duplicateConstructor = checkSource(QStringLiteral(R"(
            struct Box {
                int x;

                init(int v) {
                    x = v;
                }

                init(int other) {
                    x = other;
                }
            }

            fn int main() {
                return 0;
            }
        )"));
        QVERIFY(!duplicateConstructor.diagnostics.isEmpty());
        QVERIFY(countMessagesContaining(duplicateConstructor, QStringLiteral("duplicate constructor overload")) >= 1);

        auto duplicateMethod = checkSource(QStringLiteral(R"(
            struct Box {
                int x;

                fn int get(int v) {
                    return v;
                }

                fn int get(int other) {
                    return other;
                }
            }

            fn int main() {
                return 0;
            }
        )"));
        QVERIFY(!duplicateMethod.diagnostics.isEmpty());
        QVERIFY(countMessagesContaining(duplicateMethod, QStringLiteral("duplicate method 'get' overload")) >= 1);

        auto noMatchingConstructor = checkSource(QStringLiteral(R"(
            struct Box {
                int x;

                init(int v) {
                    x = v;
                }
            }

            fn int main() {
                Box b = Box("bad");
                return b.x;
            }
        )"));
        QVERIFY(!noMatchingConstructor.diagnostics.isEmpty());
        QVERIFY(countMessagesContaining(noMatchingConstructor, QStringLiteral("constructor parameter 'v'")) >= 1);

        auto noMatchingMethod = checkSource(QStringLiteral(R"(
            struct Box {
                int x;

                fn int get() {
                    return x;
                }

                fn int get(int add) {
                    return x + add;
                }
            }

            fn int main() {
                Box b = Box(1);
                return b.get("bad");
            }
        )"));
        QVERIFY(!noMatchingMethod.diagnostics.isEmpty());
        QVERIFY(countMessagesContaining(noMatchingMethod, QStringLiteral("no matching method 'get' overload")) >= 1);

        auto ambiguousConstructor = checkSource(QStringLiteral(R"(
            struct Box {
                int x;

                init(any a, int b) {
                    x = 1;
                }

                init(int a, any b) {
                    x = 2;
                }
            }

            fn int main() {
                Box b = Box(1, 2);
                return b.x;
            }
        )"));
        QVERIFY(!ambiguousConstructor.diagnostics.isEmpty());
        QVERIFY(countMessagesContaining(ambiguousConstructor, QStringLiteral("constructor 'Box' overload is ambiguous")) >= 1);

        auto ambiguousMethod = checkSource(QStringLiteral(R"(
            struct Box {
                int x;

                fn int choose(any a, int b) {
                    return 1;
                }

                fn int choose(int a, any b) {
                    return 2;
                }
            }

            fn int main() {
                Box b = Box(1);
                return b.choose(1, 2);
            }
        )"));
        QVERIFY(!ambiguousMethod.diagnostics.isEmpty());
        QVERIFY(countMessagesContaining(ambiguousMethod, QStringLiteral("method 'choose' overload is ambiguous")) >= 1);
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

    void acceptsCharAndAnyBuiltins()
    {
        const QString src = QStringLiteral(R"(
            struct Box {
                int value;

                init(int v) {
                    value = v;
                }
            }

            fn int inc(int x) {
                return x + 1;
            }

            fn int main() {
                char a = 'a';
                int code = char_code(a);
                char b = char_from_code(code);
                bool charOk = char_is_lower(a)
                    && char_is_upper(char_upper(a))
                    && char_is_letter(a)
                    && char_is_digit('7')
                    && char_is_alnum('8')
                    && char_is_space(' ')
                    && char_lower('A') == a
                    && char_to_str(b).len() == 1;

                any x = 7;
                any s = "kappa";
                any chars = str_to_chars("ab");
                any box = Box(3);
                any f = inc;
                bool anyOk = any_type(x).len() > 0
                    && any_debug(x).len() > 0
                    && any_is(x, "integer")
                    && any_is_int(x)
                    && !any_is_double(x)
                    && any_is_str(s)
                    && any_is_vector(chars)
                    && !any_is_struct(chars)
                    && !any_is_func(chars)
                    && any_is_struct(box)
                    && any_is(box, "struct")
                    && any_debug(box).len() > 0
                    && any_is_func(f)
                    && any_is(f, "func")
                    && any_debug(f).len() > 0;

                bool pipeOk = a |> char_upper |> char_is_upper;
                bool pipeAny = x |> any_is_int;
                bool pipeNamed = x |> any_is("i32");
                if (charOk && anyOk && pipeOk && pipeAny && pipeNamed) {
                    return code;
                }
                return 0;
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void rejectsCharAndAnyBuiltinMisuse()
    {
        auto badCharArg = checkSource(QStringLiteral("fn int main() { int x = char_code(1); return x; }"));
        QVERIFY(!badCharArg.diagnostics.isEmpty());

        auto badCodeArg = checkSource(QStringLiteral("fn int main() { char c = char_from_code(\"x\"); return 0; }"));
        QVERIFY(!badCodeArg.diagnostics.isEmpty());

        auto badAnyValue = checkSource(QStringLiteral("fn int main() { str t = any_type(1); return t.len(); }"));
        QVERIFY(!badAnyValue.diagnostics.isEmpty());

        auto badAnyDebugValue = checkSource(QStringLiteral("fn int main() { str t = any_debug(1); return t.len(); }"));
        QVERIFY(!badAnyDebugValue.diagnostics.isEmpty());

        auto badAnyExpected = checkSource(QStringLiteral(R"(
            fn int main() {
                any x = 1;
                bool ok = any_is(x, 1);
                return ok;
            }
        )"));
        QVERIFY(!badAnyExpected.diagnostics.isEmpty());

        auto badPipeAny = checkSource(QStringLiteral("fn int main() { bool ok = 1 |> any_is_int; return ok; }"));
        QVERIFY(!badPipeAny.diagnostics.isEmpty());
    }

    void acceptsStringBuiltinMethods()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                str s = build_string("hakurei", " shrine");
                bool ok = s.contains("rei") && !s.empty() && s.starts_with("hak") && s.ends_with("shrine");
                int pos = s.find("shrine");
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
                if (parsedBool) {
                    return s.len() + a.len() + b.len() + c.len() + t.len() + parts.len()
                        + joined.len() + parsed + cast<int>(parsedLong) + cast<int>(parsedDouble);
                }
                return 0;
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void rejectsBadStringBuiltinMethodArguments()
    {
        auto badNeedle = checkSource(QStringLiteral(R"(
            fn int main() {
                return "abc".find(1);
            }
        )"));
        QVERIFY(!badNeedle.diagnostics.isEmpty());

        auto badSliceIndex = checkSource(QStringLiteral(R"(
            fn int main() {
                str s = "abc".slice("x", 1);
                return s.len();
            }
        )"));
        QVERIFY(!badSliceIndex.diagnostics.isEmpty());

        auto badStartsWith = checkSource(QStringLiteral(R"(
            fn int main() {
                return "abc".starts_with(1);
            }
        )"));
        QVERIFY(!badStartsWith.diagnostics.isEmpty());

        auto badTrimArity = checkSource(QStringLiteral(R"(
            fn int main() {
                str s = "abc".trim(1);
                return s.len();
            }
        )"));
        QVERIFY(!badTrimArity.diagnostics.isEmpty());

        auto badSplit = checkSource(QStringLiteral(R"(
            fn int main() {
                vector<str> xs = "abc".split(1);
                return xs.len();
            }
        )"));
        QVERIFY(!badSplit.diagnostics.isEmpty());

        auto badJoin = checkSource(QStringLiteral(R"(
            fn int main() {
                str s = ",".join("bad");
                return s.len();
            }
        )"));
        QVERIFY(!badJoin.diagnostics.isEmpty());

        auto badParseArity = checkSource(QStringLiteral(R"(
            fn int main() {
                return "123".parse_int(1);
            }
        )"));
        QVERIFY(!badParseArity.diagnostics.isEmpty());
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

    void acceptsAnyFunctionCast()
    {
        const QString src = QStringLiteral(R"(
            fn int inc(int x) {
                return x + 1;
            }

            fn int main() {
                any f = inc;
                func int(int) g = cast<func int(int)>(f);
                return g(1);
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void acceptsAnyVectorCast()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                vector<any> xs = {1, 2, 3};
                any raw = xs;
                vector<int> ys = cast<vector<int>>(raw);
                return ys[0];
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void acceptsAnyDynamicBinaryOperators()
    {
        const QString src = QStringLiteral(R"(
            fn any operator +(any a, any b) {
                return build_string(any_debug(a), ":", any_debug(b));
            }

            fn int main() {
                any a = 4;
                any b = 5;
                any sum = a + b;
                bool less = a < b;
                bool neq = a != b;
                any suffix = "b";
                any text = "a" + suffix;
                any custom = true + "x";
                str text_s = cast<str>(text);
                str custom_s = cast<str>(custom);
                if (less && neq) {
                    return cast<int>(sum) + text_s.len() + custom_s.len();
                }
                return 0;
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void acceptsImplicitAnyCastsAtTargetTypedPositions()
    {
        const QString src = QStringLiteral(R"(
            backend MathSystem {
                fn int fast_add(int a, int b);
            }

            fn int take(int x) {
                return x + 1;
            }

            fn int ret(any x) {
                return x;
            }

            fn int main() {
                any a = 5;
                int x = a;
                a = 6;
                x = a;
                int y = take(a);
                int z = ret(a);
                vector<any> xs = {1, 2};
                any raw = xs;
                vector<int> ys = raw;
                int b = MathSystem::fast_add(a, 1);
                return x + y + z + ys[1] + b;
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void acceptsPipeHolesForFunctionsAndFunctionValues()
    {
        const QString src = QStringLiteral(R"(
            fn int add(int a, int b) {
                return a + b;
            }

            fn int bump(int& x) {
                x = x + 1;
                return x;
            }

            fn int main() {
                func int(int, int) sub = lambda [] int(int a, int b) {
                    return a - b;
                };
                int x = 3;
                int a = x |> add(10, _);
                int b = x |> add(_, _);
                int c = x |> sub(20, _);
                int d = x |> bump(_);
                return a + b + c + d;
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void acceptsPipeHolesForBuiltinFunctions()
    {
        const QString src = QStringLiteral(R"(
            fn int next(int& x) {
                x = x + 1;
                return x;
            }

            fn int main() {
                any value = 7;
                vector<any> tail = {"A", 7, true};
                int counter = 0;
                int hi = 4 |> max(9, _);
                int bounded = 7 |> clamp(_, 1, 5);
                int once = next(counter) |> max(_, _);
                bool ok = value |> any_is(_, "i32");
                str text = "x" |> build_string("pre", _, "!");
                str spread = tail |> build_string("pre", ..._);
                println(...tail);
                return hi + bounded + once + counter + text.len() + spread.len();
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void acceptsPipeHoleReceiversForMethodsAndFields()
    {
        const QString src = QStringLiteral(R"(
            struct Box {
                str text;

                init(str s) {
                    text = s;
                }

                const fn str shout() {
                    return text.trim().upper();
                }

                const fn bool same(const Box& other) {
                    return text == other.text;
                }
            }

            fn int main() {
                Box b = Box(" abel ");
                str a = " kappa " |> _.trim().upper();
                str b1 = b |> _.shout();
                str b2 = b |> _.text.trim().upper();
                bool c = "ab" |> _.contains(_);
                bool d = b |> _.same(_);
                return a.len() + b1.len() + b2.len();
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void rejectsInvalidPipeHoles()
    {
        auto outsidePipe = checkSource(QStringLiteral(R"(
            fn int main() {
                int x = _;
                return x;
            }
        )"));
        QVERIFY(countMessagesContaining(outsidePipe, QStringLiteral("'_' pipe hole is only valid")) >= 1);

        auto duplicateMutableRef = checkSource(QStringLiteral(R"(
            fn void swap_like(int& a, int& b) {
                int t = a;
                a = b;
                b = t;
            }

            fn int main() {
                int x = 1;
                x |> swap_like(_, _);
                return x;
            }
        )"));
        QVERIFY(countMessagesContaining(duplicateMutableRef,
                                        QStringLiteral("multiple mutable reference parameters")) >= 1);

        auto bareReceiver = checkSource(QStringLiteral(R"(
            fn int main() {
                int x = 1 |> _;
                return x;
            }
        )"));
        QVERIFY(countMessagesContaining(bareReceiver,
                                        QStringLiteral("pipe hole receiver must select a field or call a method")) >= 1);

        auto mutableReceiverWithSecondHole = checkSource(QStringLiteral(R"(
            fn int main() {
                vector<int> xs = {1, 2};
                xs |> _.push(_);
                return 0;
            }
        )"));
        QVERIFY(countMessagesContaining(mutableReceiverWithSecondHole,
                                        QStringLiteral("same hole multiple times")) >= 1);

        auto mutableMethodArgWithSecondHole = checkSource(QStringLiteral(R"(
            struct Box {
                int value;

                init(int x) {
                    value = x;
                }

                const fn int touch(Box& other) {
                    other.value = other.value + 1;
                    return other.value;
                }
            }

            fn int main() {
                Box b = Box(1);
                int y = b |> _.touch(_);
                return y;
            }
        )"));
        QVERIFY(countMessagesContaining(mutableMethodArgWithSecondHole,
                                        QStringLiteral("same hole multiple times")) >= 1);

        auto pipeBuiltinSpreadNonAnyVector = checkSource(QStringLiteral(R"(
            fn int main() {
                vector<int> xs = {1, 2};
                str text = xs |> build_string(..._);
                return text.len();
            }
        )"));
        QVERIFY(countMessagesContaining(pipeBuiltinSpreadNonAnyVector,
                                        QStringLiteral("spread argument expects vector<any>")) >= 1);

        auto pipeBuiltinSpreadFixed = checkSource(QStringLiteral(R"(
            fn int main() {
                vector<any> xs = {1, 2};
                int x = xs |> max(..._);
                return x;
            }
        )"));
        QVERIFY(countMessagesContaining(pipeBuiltinSpreadFixed,
                                        QStringLiteral("spread arguments are only supported for any... builtin functions")) >= 1);
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

    void acceptsScanBuiltin()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                int x;
                str s;
                double d;
                bool ok;
                char c;
                any a;
                scan(&x, &s, &d, &ok, &c, &a);
                &x |> scan(&s);
                return 0;
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void acceptsFileAndPathBuiltins()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                str dir = "/tmp/abel";
                str path = build_string(dir, "/note.txt");
                mkdirs(dir);
                write_text(path, "alpha");
                append_text(path, "beta");
                str text = read_text(path);
                vector<str> lines = {"beta", "gamma"};
                write_lines(path, lines);
                vector<str> read = read_lines(path);
                str copy = path_join(dir, "copy.txt");
                str moved = path_join(dir, "moved.txt");
                copy_file(path, copy);
                copy |> move_path(moved);
                remove_path(moved);
                str parent = path_dirname(path);
                str base = path_basename(path);
                str ext = path_ext(path);
                str abs = path_absolute(path);
                str clean = path_clean(build_string(dir, "/../abel/note.txt"));
                str cwd = current_dir();
                bool hasPath = env_exists("PATH");
                str pathEnv = env_get("PATH");
                bool ok = path_exists(path) && path_is_file(path) && path_is_dir(dir);
                bool pipeOk = path |> path_exists;
                path |> write_text("delta");
                if (ok && pipeOk) {
                    return text.len() + read.len() + parent.len() + base.len() + ext.len()
                        + abs.len() + clean.len() + cwd.len() + pathEnv.len() + 2;
                }
                return 0;
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void rejectsFileAndPathBuiltinMisuse()
    {
        auto badReadPath = checkSource(QStringLiteral("fn int main() { str s = read_text(1); return s.len(); }"));
        QVERIFY(!badReadPath.diagnostics.isEmpty());

        auto badWriteContent = checkSource(QStringLiteral("fn int main() { write_text(\"x\", 1); return 0; }"));
        QVERIFY(!badWriteContent.diagnostics.isEmpty());

        auto badAppendContent = checkSource(QStringLiteral("fn int main() { append_text(\"x\", 1); return 0; }"));
        QVERIFY(!badAppendContent.diagnostics.isEmpty());

        auto badCopyDst = checkSource(QStringLiteral("fn int main() { copy_file(\"x\", 1); return 0; }"));
        QVERIFY(!badCopyDst.diagnostics.isEmpty());

        auto badJoinChild = checkSource(QStringLiteral("fn int main() { str p = path_join(\"x\", 1); return p.len(); }"));
        QVERIFY(!badJoinChild.diagnostics.isEmpty());

        auto badCurrentDirArity = checkSource(QStringLiteral("fn int main() { str p = current_dir(\"x\"); return p.len(); }"));
        QVERIFY(!badCurrentDirArity.diagnostics.isEmpty());

        auto badWriteLines = checkSource(QStringLiteral(R"(
            fn int main() {
                vector<int> xs = {1};
                write_lines("x", xs);
                return 0;
            }
        )"));
        QVERIFY(!badWriteLines.diagnostics.isEmpty());

        auto badArity = checkSource(QStringLiteral("fn int main() { bool ok = path_exists(); return ok; }"));
        QVERIFY(!badArity.diagnostics.isEmpty());

        auto badPipePath = checkSource(QStringLiteral("fn int main() { str s = 1 |> read_text; return s.len(); }"));
        QVERIFY(!badPipePath.diagnostics.isEmpty());
    }

    void acceptsMathBuiltins()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                int a = abs(-7);
                double root = sqrt(9);
                double shaped = floor(3.9) + ceil(3.1) + round(3.5) + trunc(3.9);
                double powered = pow(2, 3) + exp(0) + log(1) + log10(100);
                double trig = sin(0) + cos(0) + tan(0) + asin(0) + acos(1) + atan(0) + atan2(0, 1);
                int divisors = gcd(54, 24) + lcm(6, 8);
                double lo = min(4, 5.5);
                int hi = max(2, 3);
                int bounded = clamp(10, 0, 7);
                int pipeHi = 4 |> max(9);
                int pipeBounded = 3 |> clamp(1, 9);
                int pipeGcd = 54 |> gcd(24);
                return a + cast<int>(root + shaped + powered + trig + lo) + hi + bounded + pipeHi + pipeBounded + divisors + pipeGcd;
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void rejectsMathBuiltinMisuse()
    {
        auto nonNumeric = checkSource(QStringLiteral("fn int main() { double x = sqrt(\"x\"); return 0; }"));
        QVERIFY(!nonNumeric.diagnostics.isEmpty());

        auto badArity = checkSource(QStringLiteral("fn int main() { double x = pow(2); return 0; }"));
        QVERIFY(!badArity.diagnostics.isEmpty());

        auto pipeBadArg = checkSource(QStringLiteral("fn int main() { int x = 3 |> clamp(\"x\", 9); return x; }"));
        QVERIFY(!pipeBadArg.diagnostics.isEmpty());

        auto badGcdType = checkSource(QStringLiteral("fn int main() { int x = gcd(1.5, 2); return x; }"));
        QVERIFY(!badGcdType.diagnostics.isEmpty());

        auto badPipeGcdType = checkSource(QStringLiteral("fn int main() { int x = 1.5 |> gcd(2); return x; }"));
        QVERIFY(!badPipeGcdType.diagnostics.isEmpty());
    }

    void rejectsScanBuiltinMisuse()
    {
        auto nonPointer = checkSource(QStringLiteral("fn int main() { int x; scan(x); return 0; }"));
        QVERIFY(!nonPointer.diagnostics.isEmpty());

        auto constPointer = checkSource(QStringLiteral("fn int main() { const int x = 0; scan(&x); return 0; }"));
        QVERIFY(!constPointer.diagnostics.isEmpty());

        auto pipeNonPointer = checkSource(QStringLiteral("fn int main() { int x; x |> scan(); return 0; }"));
        QVERIFY(!pipeNonPointer.diagnostics.isEmpty());

        auto unsupported = checkSource(QStringLiteral(R"(
            struct Box {
                int value;
            }

            fn int main() {
                Box b = Box(1);
                scan(&b);
                return 0;
            }
        )"));
        QVERIFY(!unsupported.diagnostics.isEmpty());
    }

    void acceptsTestBuiltins()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                test_assert(true, "ok");
                test_eq(4, 4.0, "numeric ok");
                test_ne("a", "b");
                test_close(3.14159, 3.14, 0.01, "close ok");
                true |> test_assert(" via pipe");
                4 |> test_eq(4, " pipe eq");
                3.14159 |> test_close(3.14, 0.01, " pipe close");
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
        auto badCloseArity = checkSource(QStringLiteral("fn int main() { test_close(1, 1); return 0; }"));
        QVERIFY(!badCloseArity.diagnostics.isEmpty());
        auto badCloseActual = checkSource(QStringLiteral("fn int main() { test_close(\"x\", 1, 0.1); return 0; }"));
        QVERIFY(!badCloseActual.diagnostics.isEmpty());
        auto badCloseEps = checkSource(QStringLiteral("fn int main() { test_close(1, 1, \"eps\"); return 0; }"));
        QVERIFY(!badCloseEps.diagnostics.isEmpty());
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

    void parserErrorDoesNotCascadeIntoMissingReturn()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                int x = 1
                return x;
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QCOMPARE(result.diagnostics.size(), 1);
        QVERIFY(result.diagnostics.front().message.contains(QStringLiteral("expected ';'")));
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

    void acceptsVectorInsertEraseFindAndSort()
    {
        const QString src = QStringLiteral(R"(
            fn int main() {
                vector<int> xs = {3, 1, 2, 2};
                xs.insert(1, 2);
                int at = xs.find(2);
                bool has = xs.contains(2);
                int copies = xs.count(2);
                vector<int> mid = xs.slice(1, 3);
                xs.extend(mid);
                xs.sort();
                int lb = xs.lower_bound(2);
                int ub = xs.upper_bound(2);
                bool sortedHas = xs.binary_search(2);
                xs.unique();
                xs.reverse();
                int removed = xs.erase(0);
                if (has && sortedHas) {
                    return at + copies + mid.len() + lb + ub + removed + xs[0];
                }
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

    void rejectsVectorInsertFindAndSortBadTypes()
    {
        auto badInsert = checkSource(QStringLiteral(R"(
            fn int main() {
                vector<int> xs = {1};
                xs.insert(0, "bad");
                return 0;
            }
        )"));
        QVERIFY(!badInsert.diagnostics.isEmpty());

        auto badFind = checkSource(QStringLiteral(R"(
            fn int main() {
                vector<int> xs = {1};
                return xs.find("bad");
            }
        )"));
        QVERIFY(!badFind.diagnostics.isEmpty());

        auto badContains = checkSource(QStringLiteral(R"(
            fn int main() {
                vector<int> xs = {1};
                return xs.contains("bad");
            }
        )"));
        QVERIFY(!badContains.diagnostics.isEmpty());

        auto badExtend = checkSource(QStringLiteral(R"(
            fn int main() {
                vector<int> xs = {1};
                vector<str> ys = {"bad"};
                xs.extend(ys);
                return 0;
            }
        )"));
        QVERIFY(!badExtend.diagnostics.isEmpty());

        auto badSlice = checkSource(QStringLiteral(R"(
            fn int main() {
                vector<int> xs = {1};
                vector<int> ys = xs.slice("bad", 1);
                return ys.len();
            }
        )"));
        QVERIFY(!badSlice.diagnostics.isEmpty());

        auto badLowerBound = checkSource(QStringLiteral(R"(
            fn int main() {
                vector<int> xs = {1};
                return xs.lower_bound("bad");
            }
        )"));
        QVERIFY(!badLowerBound.diagnostics.isEmpty());

        auto badSort = checkSource(QStringLiteral(R"(
            struct Box {
                int x;
            }

            fn int main() {
                vector<Box> xs;
                xs.sort();
                return 0;
            }
        )"));
        QVERIFY(!badSort.diagnostics.isEmpty());

        auto badBinarySearch = checkSource(QStringLiteral(R"(
            struct Box {
                int x;
            }

            fn int main() {
                vector<Box> xs;
                Box b;
                bool has = xs.binary_search(b);
                return 0;
            }
        )"));
        QVERIFY(!badBinarySearch.diagnostics.isEmpty());

        auto constInsert = checkSource(QStringLiteral(R"(
            fn int main() {
                const vector<int> xs = {1};
                xs.insert(0, 2);
                return 0;
            }
        )"));
        QVERIFY(!constInsert.diagnostics.isEmpty());

        auto constReverse = checkSource(QStringLiteral(R"(
            fn int main() {
                const vector<int> xs = {1};
                xs.reverse();
                return 0;
            }
        )"));
        QVERIFY(!constReverse.diagnostics.isEmpty());
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

    void acceptsPrivateStructInternalsFromOwnMethods()
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
                    Vault other = Vault(1);
                    return secret + other.leak();
                }
            }

            fn int main() {
                Vault v = Vault(7);
                return v.get();
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void rejectsExternalPrivateStructMembers()
    {
        auto privateField = checkSource(QStringLiteral(R"(
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

        auto privateMethod = checkSource(QStringLiteral(R"(
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

        auto privateConstructor = checkSource(QStringLiteral(R"(
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

        auto privatePositionalField = checkSource(QStringLiteral(R"(
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

        auto privateDefaultResize = checkSource(QStringLiteral(R"(
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

    void acceptsUserBinaryOperators()
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
                if (c == Point(4, 6)) {
                    Size s = Size(5, 6) + Size(7, 8);
                    return c.x + c.y + s.w + s.h;
                }
                return 0;
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

    void rejectsBadUserBinaryOperators()
    {
        auto wrongArity = checkSource(QStringLiteral(R"(
            struct Point {
                int x;
            }

            fn Point operator +(Point a) {
                return a;
            }

            fn int main() {
                return 0;
            }
        )"));
        QVERIFY(!wrongArity.diagnostics.isEmpty());

        auto badOperand = checkSource(QStringLiteral(R"(
            struct Point {
                int x;
            }

            fn Point operator +(Point a, Point b) {
                return Point(a.x + b.x);
            }

            fn int main() {
                Point a = Point(1);
                Point c = a + 1;
                return c.x;
            }
        )"));
        QVERIFY(!badOperand.diagnostics.isEmpty());

        auto duplicateSignature = checkSource(QStringLiteral(R"(
            struct Point {
                int x;
            }

            fn Point operator +(Point a, Point b) {
                return a;
            }

            fn Point operator +(Point a, Point b) {
                return b;
            }

            fn int main() {
                return 0;
            }
        )"));
        QVERIFY(!duplicateSignature.diagnostics.isEmpty());

        auto ambiguous = checkSource(QStringLiteral(R"(
            struct Box {
                int x;
            }

            fn Box operator +(Box a, any b) {
                return a;
            }

            fn Box operator +(any a, Box b) {
                return b;
            }

            fn int main() {
                Box a = Box(1);
                Box c = a + a;
                return c.x;
            }
        )"));
        QVERIFY(!ambiguous.diagnostics.isEmpty());
    }

    void acceptsThisMethodNestedStructAssignmentAndFunctionValues()
    {
        const QString src = QStringLiteral(R"(
            module app.main;

            struct A {
                int x;

                init(int v) {
                    x = v;
                }

                fn int hit(int a, int b) {
                    return x + a + b;
                }

                fn int call() {
                    return this.hit(1, 2);
                }
            }

            struct B {
                A a;

                init() {
                    a = A(7);
                }
            }

            fn int f(str s) {
                return s.len();
            }

            fn int main() {
                A a = A(4);
                B b = B();
                func int(str) cb = f;
                return a.call() + b.a.x + cb("abc");
            }
        )");
        auto result = checkSource(src);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
    }

};

QTEST_MAIN(AbelTypeCheckerTests)

#include "test_typechecker.moc"
