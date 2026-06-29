#include "abelcore/interpreter.h"
#include "abelcore/lexer.h"
#include "abelcore/parser.h"
#include "abelcore/typechecker.h"

#include <QtTest/QtTest>

class AbelComplexProgramTests final : public QObject {
    Q_OBJECT

private:
    struct RunResult {
        QList<abel::Diagnostic> checkDiagnostics;
        abel::InterpreterResult run;
    };

    struct BadResult {
        QList<abel::Diagnostic> diagnostics;
        int exitCode = 0;
    };

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

    static RunResult checkAndRun(const QString& src)
    {
        RunResult out;
        abel::Lexer lexer;
        auto lexed = lexer.lex(QStringLiteral("<complex>"), src);
        if (!lexed.diagnostics.isEmpty()) {
            out.checkDiagnostics = lexed.diagnostics;
            out.run.exitCode = 1;
            out.run.diagnostics = lexed.diagnostics;
            return out;
        }

        abel::Parser parser;
        auto parsed = parser.parse(lexed.tokens);
        if (!parsed.diagnostics.isEmpty()) {
            out.checkDiagnostics = parsed.diagnostics;
            out.run.exitCode = 1;
            out.run.diagnostics = parsed.diagnostics;
            return out;
        }

        tagInlineModules(*parsed.program);
        abel::TypeChecker checker;
        auto checked = checker.check(*parsed.program);
        out.checkDiagnostics = checked.diagnostics;
        if (!out.checkDiagnostics.isEmpty()) {
            out.run.exitCode = 1;
            out.run.diagnostics = out.checkDiagnostics;
            return out;
        }

        abel::Interpreter interpreter;
        out.run = interpreter.run(*parsed.program);
        return out;
    }

    static BadResult checkOrRunBad(const QString& src)
    {
        BadResult out;
        abel::Lexer lexer;
        auto lexed = lexer.lex(QStringLiteral("<complex-bad>"), src);
        if (!lexed.diagnostics.isEmpty()) {
            out.diagnostics = lexed.diagnostics;
            out.exitCode = 1;
            return out;
        }

        abel::Parser parser;
        auto parsed = parser.parse(lexed.tokens);
        if (!parsed.diagnostics.isEmpty()) {
            out.diagnostics = parsed.diagnostics;
            out.exitCode = 1;
            return out;
        }

        tagInlineModules(*parsed.program);
        abel::TypeChecker checker;
        auto checked = checker.check(*parsed.program);
        if (!checked.diagnostics.isEmpty()) {
            out.diagnostics = checked.diagnostics;
            out.exitCode = 1;
            return out;
        }

        abel::Interpreter interpreter;
        auto run = interpreter.run(*parsed.program);
        out.diagnostics = run.diagnostics;
        out.exitCode = run.exitCode;
        return out;
    }

    static bool diagnosticsContain(const QList<abel::Diagnostic>& diagnostics, const QString& needle)
    {
        for (const auto& d : diagnostics) {
            if (d.code.contains(needle) || d.message.contains(needle))
                return true;
        }
        return false;
    }

private slots:
    void heavyPrograms_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("src");
        QTest::addColumn<int>("expectedExit");

        QTest::newRow("01_algorithms_struct_vector_loops")
            << QStringLiteral("algorithm vector + struct + loops")
            << QStringLiteral(R"(
                struct Acc {
                    int total;
                    init() { total = 0; }
                    fn void add(int x) { total = total + x; }
                    const fn int get() { return total; }
                }

                fn int fold(vector<int> xs) {
                    Acc acc = Acc();
                    for (x in xs) {
                        acc.add(x);
                    }
                    return acc.get();
                }

                fn int main() {
                    vector<int> xs = {1, 2, 3, 4, 5};
                    int even = 0;
                    for (int i = 0; i < xs.len(); i = i + 1) {
                        if (xs[i] % 2 == 0) {
                            even = even + xs[i];
                        }
                    }
                    return fold(xs) + even;
                }
            )")
            << 21;

        QTest::newRow("02_recursion_enum_alias_string")
            << QStringLiteral("recursion + enum + alias + build_string")
            << QStringLiteral(R"(
                enum Mode { Small, Big }
                type Score = int;

                fn int fib(int n) {
                    if (n <= 1) { return n; }
                    return fib(n - 1) + fib(n - 2);
                }

                fn Score score(Mode m, int n) {
                    if (m == Mode.Big) { return fib(n) + 10; }
                    return fib(n);
                }

                fn int main() {
                    str s = build_string("fib=", score(Mode.Big, 6));
                    if (s == "fib=18") {
                        return score(Mode.Big, 6) + s.len();
                    }
                    return 0;
                }
            )")
            << 24;

        QTest::newRow("03_lambda_captures_vector_mutation")
            << QStringLiteral("lambda captures + vector mutation")
            << QStringLiteral(R"(
                fn int main() {
                    vector<int> xs = {1, 2, 3};
                    int base = 5;
                    func int(int) add = lambda [base, &xs] int(int i) {
                        xs[i] = xs[i] + base;
                        return xs[i];
                    };
                    int a = add(0);
                    base = 100;
                    int b = add(1);
                    return a + b + xs[2];
                }
            )")
            << 16;

        QTest::newRow("04_pipe_named_default_spread")
            << QStringLiteral("pipe + named/default + spread")
            << QStringLiteral(R"(
                fn int inc(int x, int by = 1) { return x + by; }
                fn int count(any... xs) { return xs.len(); }
                fn int sum3(int a, int b, int c = 30) { return a + b + c; }

                fn int main() {
                    vector<any> tail = {"x", true, 7};
                    int a = 3 |> inc(x: _, by: 4);
                    int b = 5 |> inc(by: 2);
                    int c = 1 |> sum3(a: _, b: 2);
                    int d = tail |> count("head", ..._);
                    return a + b + c + d;
                }
            )")
            << 51;

        QTest::newRow("05_any_cast_vector_struct_debug")
            << QStringLiteral("any/cast vector + struct debug")
            << QStringLiteral(R"(
                struct Box {
                    int value;
                    init(int v) { value = v; }
                }

                fn int main() {
                    vector<any> raw = {1, 2, 3};
                    any boxedVector = raw;
                    vector<int> ints = cast<vector<int>>(boxedVector);
                    any boxedStruct = Box(9);
                    Box b = cast<Box>(boxedStruct);
                    if (any_is(boxedStruct, "struct") && any_debug(boxedStruct) == "<struct Box>") {
                        return ints[0] + ints[1] + ints[2] + b.value;
                    }
                    return 0;
                }
            )")
            << 15;

        QTest::newRow("06_dynamic_binary_operator_any_catchall")
            << QStringLiteral("dynamic operator with any catch-all")
            << QStringLiteral(R"(
                fn any operator +(any a, any b) {
                    return build_string(any_debug(a), ":", any_debug(b));
                }

                fn int main() {
                    any a = 10;
                    any b = 5;
                    any n = a + b;
                    any s = true + "x";
                    str text = cast<str>(s);
                    if (text == "true:x" && a > b) {
                        return cast<int>(n) + text.len();
                    }
                    return 0;
                }
            )")
            << 21;

        QTest::newRow("07_struct_overload_operator")
            << QStringLiteral("struct constructor overload + operator overload")
            << QStringLiteral(R"(
                struct Point {
                    int x;
                    int y;
                    init(int v) { x = v; y = v; }
                    init(int a, int b) { x = a; y = b; }
                }

                fn Point operator +(Point a, Point b) {
                    return Point(a.x + b.x, a.y + b.y);
                }

                fn bool operator ==(Point a, Point b) {
                    return a.x == b.x && a.y == b.y;
                }

                fn int main() {
                    Point a = Point(3);
                    Point b = Point(4, 5);
                    Point c = a + b;
                    if (c == Point(7, 8)) {
                        return c.x + c.y;
                    }
                    return 0;
                }
            )")
            << 15;

        QTest::newRow("08_private_public_methods")
            << QStringLiteral("private/public methods and internal access")
            << QStringLiteral(R"(
                struct Vault {
                private:
                    int secret;
                    fn int leak() { return secret; }

                public:
                    init(int x) { secret = x; }
                    fn void bump() { secret = secret + 1; }
                    fn int reveal_with(Vault other) { return secret + other.leak(); }
                }

                fn int main() {
                    Vault a = Vault(10);
                    Vault b = Vault(20);
                    a.bump();
                    return a.reveal_with(b);
                }
            )")
            << 31;

        QTest::newRow("09_pointer_ref_const_chain")
            << QStringLiteral("pointer/ref/const chain")
            << QStringLiteral(R"(
                fn void add_to(int& x, const int& by) {
                    x = x + by;
                }

                fn int main() {
                    int x = 10;
                    int y = 7;
                    int* p = &x;
                    *p = *p + 3;
                    add_to(x, y);
                    const int& r = x;
                    return r;
                }
            )")
            << 20;

        QTest::newRow("10_modules_export_alias_qualified")
            << QStringLiteral("module/export/use alias/qualified")
            << QStringLiteral(R"(
                module lib.math;
                export fn int scale(int x, int by = 2) { return x * by; }
                export struct Pair {
                    int a;
                    int b;
                }

                module app.main;
                use lib.math as M;

                fn int main() {
                    M::Pair p = M::Pair(4, 5);
                    int x = M::scale(p.a, by: p.b);
                    int y = 3 |> M::scale(x: _, by: 4);
                    return x + y;
                }
            )")
            << 32;

        QTest::newRow("11_string_char_vector_roundtrip")
            << QStringLiteral("string/char/vector roundtrip")
            << QStringLiteral(R"(
                fn int main() {
                    str s = " abel ";
                    str t = s.trim().upper();
                    vector<char> cs = str_to_chars(t);
                    cs.push('!');
                    str out = chars_to_str(cs);
                    if (out.contains("ABEL")) {
                        vector<char> outChars = str_to_chars(out);
                        return out.len() + char_code(outChars[0]);
                    }
                    return 0;
                }
            )")
            << 70;

        QTest::newRow("12_vector_sort_find_insert_erase")
            << QStringLiteral("vector sort/find/insert/erase")
            << QStringLiteral(R"(
                fn int main() {
                    vector<int> xs = {5, 1, 4};
                    xs.insert(1, 3);
                    xs.sort();
                    int pos = xs.find(4);
                    xs.erase(0);
                    xs.push(9);
                    return xs[0] + xs[1] + xs[2] + pos + xs.len();
                }
            )")
            << 18;

        QTest::newRow("13_control_flow_repeat_break_continue")
            << QStringLiteral("repeat/while/for break continue")
            << QStringLiteral(R"(
                fn int main() {
                    int sum = 0;
                    repeat (4) {
                        sum = sum + 2;
                    }
                    int i = 0;
                    while (i < 6) {
                        i = i + 1;
                        if (i == 2) { continue; }
                        if (i == 5) { break; }
                        sum = sum + i;
                    }
                    return sum;
                }
            )")
            << 16;

        QTest::newRow("14_function_values_overload_resolution")
            << QStringLiteral("function values + overload resolution")
            << QStringLiteral(R"(
                fn int pick(int x) { return x + 1; }
                fn int pick(str s) { return s.len(); }
                fn int twice_int(int x) { return x * 2; }
                fn str twice_str(str s) { return s + s; }
                fn int apply(func int(int) f, int x) { return f(x); }

                fn int main() {
                    int direct = pick(3) + pick("abc");
                    func int(int) f = twice_int;
                    func str(str) g = twice_str;
                    str text = g("ab");
                    return direct + apply(f, 11) + text.len();
                }
            )")
            << 33;

        QTest::newRow("15_any_vector_dynamic_indexing")
            << QStringLiteral("any vector dynamic [] and []=")
            << QStringLiteral(R"(
                fn int main() {
                    vector<any> xs = {1, "xx", 3};
                    any raw = xs;
                    any idx = 1;
                    str text = cast<str>(raw[idx]);
                    raw[0] = 10;
                    any j = 2;
                    return cast<int>(raw[0]) + text.len() + cast<int>(raw[j]);
                }
            )")
            << 15;

        QTest::newRow("16_complex_pipeline_all_dynamic_edges")
            << QStringLiteral("pipeline combining any/cast/lambda/operator/vector")
            << QStringLiteral(R"(
                fn int inc(int x, int by = 1) { return x + by; }
                fn any operator +(any a, any b) {
                    return build_string(any_debug(a), any_debug(b));
                }

                struct Box {
                    int v;
                    init(int x) { v = x; }
                    const fn int add(int x) { return v + x; }
                }

                fn int main() {
                    Box b = Box(5);
                    func int(int) f = lambda [b] int(int x) {
                        return b.add(x);
                    };
                    any af = f;
                    func int(int) restored = cast<func int(int)>(af);
                    vector<any> tail = {2, 3};
                    any raw = tail;
                    int a = 4 |> inc(by: 6);
                    any s = "A" + true;
                    str text = cast<str>(s);
                    return restored(a) + cast<int>(raw[0]) + cast<int>(raw[1]) + text.len();
                }
            )")
            << 25;

        QTest::newRow("17_v13_hydraulic_dynamic_literals_and_pipe")
            << QStringLiteral("v1.3 hydraulic dynamic literals + generalized pipe")
            << QStringLiteral(R"(
                struct Box {
                    int value;
                    init(int v) { value = v; }
                }

                fn int main() {
                    any row = [{"name" = "Nitori", "score" = 39, "box" = Box(4)}];
                    any pair = row |> [[_["name"], _["score"] + 3]];
                    any projected = row |> [{"name" = _["name"], "ok" = _["score"] < 42}];
                    row |> (_["score"] = cast<int>(_["score"]) + 3);
                    int water = 2;
                    water |> (_ = _ + _);
                    Box b = cast<Box>(row["box"]);
                    if (cast<str>(pair[0]) == "Nitori"
                        && cast<int>(pair[1]) == 42
                        && cast<bool>(projected["ok"])
                        && any_is(row, "dynamic:strmap")
                        && water == 4) {
                        return cast<int>(row["score"]) + b.value + water;
                    }
                    return 0;
                }
            )")
            << 50;

        QTest::newRow("18_v13_nested_hydraulic_routes")
            << QStringLiteral("v1.3 nested tuple/strmap routes")
            << QStringLiteral(R"(
                fn int main() {
                    any flow = [[
                        [{"name" = "alpha", "v" = 5}],
                        [{"name" = "beta", "v" = 7}]
                    ]];
                    any first = flow[0];
                    first["v"] = cast<int>(first["v"]) + 10;
                    any out = flow |> [[_[0]["name"], _[1]["v"], _[0]["v"] + _[1]["v"]]];
                    if (cast<str>(out[0]) == "alpha") {
                        return cast<int>(out[1]) + cast<int>(out[2]);
                    }
                    return 0;
                }
            )")
            << 29;

        QTest::newRow("19_v13_struct_payload_in_strmap")
            << QStringLiteral("v1.3 strmap carries struct payload")
            << QStringLiteral(R"(
                struct Gate {
                    int bias;
                    init(int b) { bias = b; }
                    const fn int push(int x) { return x + bias; }
                }

                fn int main() {
                    any row = [{"x" = 10, "gate" = Gate(6)}];
                    Gate g = cast<Gate>(row["gate"]);
                    row["x"] = g.push(cast<int>(row["x"]));
                    any pair = row |> [[_["x"], _["x"] * 2]];
                    return cast<int>(pair[0]) + cast<int>(pair[1]);
                }
            )")
            << 48;

        QTest::newRow("20_v13_vector_of_dynamic_rows")
            << QStringLiteral("v1.3 vector<any> dynamic rows")
            << QStringLiteral(R"(
                fn int main() {
                    any a = [{"n" = "a", "s" = 3}];
                    any b = [{"n" = "b", "s" = 5}];
                    any c = [{"n" = "c", "s" = 7}];
                    vector<any> rows = {a, b, c};
                    int sum = 0;
                    for (r in rows) {
                        sum = sum + cast<int>(r["s"]);
                    }
                    str name = cast<str>(rows[2]["n"]);
                    return sum + name.len();
                }
            )")
            << 16;

        QTest::newRow("21_v13_pipe_projection_record")
            << QStringLiteral("v1.3 generalized pipe projects record")
            << QStringLiteral(R"(
                fn int main() {
                    any row = [{"name" = "kappa", "score" = 37}];
                    any projected = row |> [{"label" = build_string(_["name"], ":", _["score"]), "ok" = _["score"] >= 37}];
                    int bonus = 0;
                    if (cast<bool>(projected["ok"])) {
                        bonus = 10;
                    }
                    str label = cast<str>(projected["label"]);
                    return label.len() + bonus;
                }
            )")
            << 18;

        QTest::newRow("22_v13_callback_inside_tuple")
            << QStringLiteral("v1.3 tuple carries callback")
            << QStringLiteral(R"(
                fn int add2(int x) { return x + 2; }

                fn int apply(any packet, int x) {
                    func int(int) f = cast<func int(int)>(packet[0]);
                    return f(x) + cast<int>(packet[1]);
                }

                fn int main() {
                    any packet = [[add2, 5]];
                    return apply(packet, 35);
                }
            )")
            << 42;

        QTest::newRow("23_v13_pipe_writeback_from_tuple")
            << QStringLiteral("v1.3 pipe assignment writes lvalue from dynamic tuple")
            << QStringLiteral(R"(
                fn int main() {
                    int x = 4;
                    any t = [[x, x + 1]];
                    t[0] = 10;
                    x |> (_ = cast<int>(t[0]) + cast<int>(t[1]));
                    return x;
                }
            )")
            << 15;

        QTest::newRow("24_v13_strmap_set_new_key_and_tuple_set")
            << QStringLiteral("v1.3 strmap set and tuple set")
            << QStringLiteral(R"(
                fn int main() {
                    any m = [{"a" = 1}];
                    m["b"] = 2;
                    any t = [[m["a"], m["b"]]];
                    t[1] = cast<int>(t[1]) + 20;
                    return cast<int>(t[0]) + cast<int>(t[1]);
                }
            )")
            << 23;

        QTest::newRow("25_v13_dynamic_identity_and_debug")
            << QStringLiteral("v1.3 dynamic identity equality and debug")
            << QStringLiteral(R"(
                fn int main() {
                    any t = [[1]];
                    any same = t;
                    any other = [[1]];
                    int ok = 0;
                    if (t == same) {
                        ok = ok + 10;
                    }
                    if (t != other) {
                        ok = ok + 20;
                    }
                    if (any_debug(t) == "<tuple len=1>") {
                        ok = ok + 3;
                    }
                    return ok;
                }
            )")
            << 33;

        QTest::newRow("26_v13_dynamic_type_dispatch")
            << QStringLiteral("v1.3 any_type/any_is on nested water objects")
            << QStringLiteral(R"(
                fn int main() {
                    any m = [{"tuple" = [[2, 3]], "tag" = "x"}];
                    any t = m["tuple"];
                    if (any_type(m) == "dynamic:strmap" && any_is(t, "dynamic:tuple")) {
                        return cast<int>(t[0]) * 10 + cast<int>(t[1]);
                    }
                    return 0;
                }
            )")
            << 23;

        QTest::newRow("27_v13_old_and_new_pipe_mix")
            << QStringLiteral("v1.3 old callable pipe and generalized pipe coexist")
            << QStringLiteral(R"(
                fn int inc(int x, int by = 1) { return x + by; }
                fn int count(any... xs) { return xs.len(); }

                fn int main() {
                    vector<any> tail = {1, 2};
                    int a = 3 |> inc(x: _, by: 4);
                    int b = tail |> count("head", ..._);
                    any packet = a |> [[_, b, _ + b]];
                    return cast<int>(packet[0]) + cast<int>(packet[1]) + cast<int>(packet[2]);
                }
            )")
            << 20;

        QTest::newRow("28_v13_do_expression_nested_water_route")
            << QStringLiteral("v1.3 do expression routes nested tuple/strmap water")
            << QStringLiteral(R"(
                fn int main() {
                    any req = [{
                        "meta" = [{"name" = "req", "body" = [{"cmd" = "sum", "timeout" = 3}]}],
                        "items" = [[
                            [{"score" = 10}],
                            [{"score" = 20}]
                        ]]
                    }];

                    any response = req |> do {
                        any body = _["meta"]["body"];
                        any items = _["items"];
                        int left = cast<int>(items[0]["score"]);
                        int right = cast<int>(items[1]["score"]);
                        str cmd = cast<str>(body["cmd"]);
                        return [{
                            "cmd" = cmd,
                            "total" = left + right + cast<int>(body["timeout"]),
                            "nested" = [[body, items]]
                        }];
                    };

                    any nested = response["nested"];
                    any items2 = nested[1];
                    items2[0]["score"] = cast<int>(items2[0]["score"]) + 2;
                    return cast<int>(response["total"])
                        + cast<int>(req["items"][0]["score"])
                        + cast<int>(nested[0]["timeout"]);
                }
            )")
            << 48;

        QTest::newRow("29_v13_nested_do_pipe_mixed_flow")
            << QStringLiteral("v1.3 nested do and pipe mixed dynamic information flow")
            << QStringLiteral(R"(
                fn int main() {
                    any source = [[
                        [{"v" = 1}],
                        [{"v" = 2}],
                        [{"v" = 3}]
                    ]];

                    any out = source |> do {
                        any a = _[0];
                        any b = _[1];
                        any c = _[2];
                        a["v"] = cast<int>(a["v"]) + 10;
                        any inner = b |> do {
                            int bv = cast<int>(_["v"]);
                            return [{"bv" = bv, "pair" = [[a, _]]}];
                        };
                        int sum = cast<int>(a["v"]) + cast<int>(inner["bv"]) + cast<int>(c["v"]);
                        return [{"sum" = sum, "inner" = inner}];
                    };

                    return cast<int>(out["sum"])
                        + cast<int>(out["inner"]["pair"][0]["v"]);
                }
            )")
            << 27;

        QTest::newRow("30_v13_compound_assignment_overload_water")
            << QStringLiteral("v1.3 compound assignment mixes overload and dynamic water")
            << QStringLiteral(R"(
                struct Acc {
                    int value;
                }

                fn void operator +=(Acc& acc, any item) {
                    acc.value += cast<int>(item["score"]);
                }

                fn int main() {
                    any items = [[
                        [{"score" = 5}],
                        [{"score" = 7}]
                    ]];
                    Acc acc = Acc(1);
                    acc += items[0];
                    acc += items[1];
                    items[0]["score"] += 3;
                    return acc.value + cast<int>(items[0]["score"]);
                }
            )")
            << 21;
    }

    void heavyPrograms()
    {
        QFETCH(QString, name);
        QFETCH(QString, src);
        QFETCH(int, expectedExit);

        auto result = checkAndRun(src);
        for (const auto& d : result.checkDiagnostics)
            qWarning() << name << "check" << d.code << d.message;
        for (const auto& d : result.run.diagnostics)
            qWarning() << name << "run" << d.code << d.message;
        QVERIFY2(result.checkDiagnostics.isEmpty(), qPrintable(name));
        QVERIFY2(result.run.diagnostics.isEmpty(), qPrintable(name));
        QCOMPARE(result.run.exitCode, expectedExit);
    }

    void cornerPrograms_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("src");
        QTest::addColumn<QString>("needle");

        QTest::newRow("17_any_vector_bad_element_cast")
            << QStringLiteral("vector element cast failure")
            << QStringLiteral(R"(
                fn int main() {
                    vector<any> xs = {1, "bad", 3};
                    any raw = xs;
                    vector<int> ys = cast<vector<int>>(raw);
                    return ys.len();
                }
            )")
            << QStringLiteral("vector element 1");

        QTest::newRow("18_dynamic_operator_bad_types")
            << QStringLiteral("dynamic operator bad types")
            << QStringLiteral(R"(
                fn int main() {
                    any a = true;
                    any b = "x";
                    any c = a - b;
                    return cast<int>(c);
                }
            )")
            << QStringLiteral("dynamic operator '-' failed");

        QTest::newRow("19_ref_binding_from_prvalue")
            << QStringLiteral("ref binding from prvalue")
            << QStringLiteral(R"(
                fn void bump(int& x) { x = x + 1; }
                fn int main() {
                    bump(1);
                    return 0;
                }
            )")
            << QStringLiteral("no matching function");

        QTest::newRow("20_private_member_access")
            << QStringLiteral("private member access")
            << QStringLiteral(R"(
                struct Vault {
                private:
                    int secret;
                public:
                    init(int x) { secret = x; }
                }
                fn int main() {
                    Vault v = Vault(3);
                    return v.secret;
                }
            )")
            << QStringLiteral("private");

        QTest::newRow("21_ambiguous_function_value")
            << QStringLiteral("ambiguous overloaded function value")
            << QStringLiteral(R"(
                fn int pick(int x) { return x; }
                fn int pick(str s) { return s.len(); }
                fn int main() {
                    any f = pick;
                    return 0;
                }
            )")
            << QStringLiteral("overloaded");

        QTest::newRow("22_pipe_duplicate_mutable_holes")
            << QStringLiteral("pipe duplicate mutable holes")
            << QStringLiteral(R"(
                fn void swap(int& a, int& b) {
                    int t = a;
                    a = b;
                    b = t;
                }
                fn int main() {
                    int x = 1;
                    x |> swap(_, _);
                    return x;
                }
            )")
            << QStringLiteral("same hole");

        QTest::newRow("23_dynamic_index_bad_key")
            << QStringLiteral("any vector bad dynamic index")
            << QStringLiteral(R"(
                fn int main() {
                    vector<any> xs = {1};
                    any raw = xs;
                    any key = "bad";
                    return cast<int>(raw[key]);
                }
            )")
            << QStringLiteral("dynamic index");

        QTest::newRow("24_module_visibility_missing_use")
            << QStringLiteral("module visibility missing use")
            << QStringLiteral(R"(
                module lib.hidden;
                export fn int value() { return 1; }

                module app.main;
                fn int main() {
                    return lib.hidden::value();
                }
            )")
            << QStringLiteral("not visible");

        QTest::newRow("25_v13_tuple_bad_index_type")
            << QStringLiteral("v1.3 tuple bad index type")
            << QStringLiteral(R"(
                fn int main() {
                    any t = [[1, 2]];
                    return cast<int>(t["bad"]);
                }
            )")
            << QStringLiteral("tuple index must be integer");

        QTest::newRow("26_v13_tuple_index_out_of_range")
            << QStringLiteral("v1.3 tuple index out of range")
            << QStringLiteral(R"(
                fn int main() {
                    any t = [[1]];
                    return cast<int>(t[9]);
                }
            )")
            << QStringLiteral("out of range");

        QTest::newRow("27_v13_strmap_bad_key_type")
            << QStringLiteral("v1.3 strmap bad key type")
            << QStringLiteral(R"(
                fn int main() {
                    any m = [{"x" = 1}];
                    return cast<int>(m[0]);
                }
            )")
            << QStringLiteral("strmap key must be str");

        QTest::newRow("28_v13_strmap_missing_key")
            << QStringLiteral("v1.3 strmap missing key")
            << QStringLiteral(R"(
                fn int main() {
                    any m = [{"x" = 1}];
                    return cast<int>(m["missing"]);
                }
            )")
            << QStringLiteral("missing key");

        QTest::newRow("29_v13_strmap_requires_literal_key")
            << QStringLiteral("v1.3 strmap literal key must be string literal")
            << QStringLiteral(R"(
                fn int main() {
                    str k = "x";
                    any m = [{k = 1}];
                    return 0;
                }
            )")
            << QStringLiteral("expected string literal key");

        QTest::newRow("30_v13_strmap_duplicate_key")
            << QStringLiteral("v1.3 strmap duplicate key")
            << QStringLiteral(R"(
                fn int main() {
                    any m = [{"x" = 1, "x" = 2}];
                    return 0;
                }
            )")
            << QStringLiteral("duplicate strmap key");

        QTest::newRow("31_v13_do_missing_return")
            << QStringLiteral("v1.3 do expression missing return")
            << QStringLiteral(R"(
                fn int main() {
                    int x = do {
                        if (true) {
                            return 1;
                        }
                    };
                    return x;
                }
            )")
            << QStringLiteral("do expression may end without returning");
    }

    void cornerPrograms()
    {
        QFETCH(QString, name);
        QFETCH(QString, src);
        QFETCH(QString, needle);

        auto result = checkOrRunBad(src);
        for (const auto& d : result.diagnostics)
            qWarning() << name << d.code << d.message;
        QVERIFY2(!result.diagnostics.isEmpty() || result.exitCode != 0, qPrintable(name));
        QVERIFY2(diagnosticsContain(result.diagnostics, needle),
                 qPrintable(QStringLiteral("%1 did not contain '%2'").arg(name, needle)));
    }
};

QTEST_MAIN(AbelComplexProgramTests)
#include "test_complex_programs.moc"
