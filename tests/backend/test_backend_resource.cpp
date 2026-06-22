#include "abelcore/backend_interface.h"
#include "abelcore/backend_registry.h"
#include "abelcore/interpreter.h"
#include "abelcore/lexer.h"
#include "abelcore/parser.h"
#include "abelcore/resource_node.h"

#include <QtTest/QtTest>

class AbelBackendResourceTests final : public QObject {
    Q_OBJECT

private slots:
    void registryStoresFunctionDescriptors()
    {
        abel::BackendRegistry registry;
        abel::BackendFunctionDesc desc{
            QStringLiteral("MathSystem"),
            QStringLiteral("fast_add"),
            abel::makeType(abel::TypeKind::I32),
            {abel::makeType(abel::TypeKind::I32), abel::makeType(abel::TypeKind::I32)},
            false,
        };
        QVERIFY(registry.registerFunction(desc));
        QVERIFY(registry.hasBackend(QStringLiteral("MathSystem")));
        QVERIFY(registry.hasFunction(QStringLiteral("MathSystem"), QStringLiteral("fast_add")));

        const auto* found = registry.findFunction(QStringLiteral("MathSystem"), QStringLiteral("fast_add"));
        QVERIFY(found != nullptr);
        QCOMPARE(found->returnType.kind, abel::TypeKind::I32);
        QCOMPARE(found->params.size(), static_cast<size_t>(2));
    }

    void registryRejectsForeignFullSymbol()
    {
        abel::BackendRegistry registry;
        abel::Diagnostic diagnostic;
        QVERIFY(!registry.registerFunction({
            QStringLiteral("MathSystem"),
            QStringLiteral("Other.fast_add"),
            abel::makeType(abel::TypeKind::I32),
            {},
            false,
        }, &diagnostic));
        QCOMPARE(diagnostic.code, QStringLiteral("E0610"));
        QVERIFY(!registry.hasBackend(QStringLiteral("MathSystem")));
    }

    void registryReportsUnboundAndMissingSymbols()
    {
        abel::BackendRegistry registry;
        QList<abel::Diagnostic> diagnostics;

        registry.call({QStringLiteral("Missing"), QStringLiteral("fn"), {}, {}}, diagnostics);
        QCOMPARE(diagnostics.size(), 1);
        QCOMPARE(diagnostics.front().code, QStringLiteral("E0607"));

        diagnostics.clear();
        QVERIFY(registry.registerFunction({
            QStringLiteral("MathSystem"),
            QStringLiteral("fast_add"),
            abel::makeType(abel::TypeKind::I32),
            {},
            false,
        }));
        registry.call({QStringLiteral("MathSystem"), QStringLiteral("missing"), {}, {}}, diagnostics);
        QCOMPARE(diagnostics.size(), 1);
        QCOMPARE(diagnostics.front().code, QStringLiteral("E0608"));

        diagnostics.clear();
        registry.call({QStringLiteral("MathSystem"), QStringLiteral("fast_add"), {}, {}}, diagnostics);
        QCOMPARE(diagnostics.size(), 1);
        QCOMPARE(diagnostics.front().code, QStringLiteral("E0607"));
    }

    void registryCanBindRuntime()
    {
        abel::BackendRegistry registry;
        QVERIFY(registry.registerFunction({
            QStringLiteral("MathSystem"),
            QStringLiteral("fast_add"),
            abel::makeType(abel::TypeKind::I32),
            {},
            false,
        }));
        bool called = false;
        QString seenBackend;
        QString seenSymbol;
        size_t seenArgCount = 0;
        QVERIFY(registry.bindFunction(QStringLiteral("MathSystem"),
                                      QStringLiteral("fast_add"),
                                      [&called, &seenBackend, &seenSymbol, &seenArgCount](const abel::BackendCall& call, abel::AbelRuntimeContext&) {
                                          called = true;
                                          seenBackend = call.backendId;
                                          seenSymbol = call.symbol;
                                          seenArgCount = call.args.size();
                                          return abel::AbelValue::makeInt(call.args[0].asInt() + call.args[1].asInt());
                                      }));

        QList<abel::Diagnostic> diagnostics;
        auto value = registry.call({
            QStringLiteral("MathSystem"),
            QStringLiteral("fast_add"),
            {abel::AbelValue::makeInt(4), abel::AbelValue::makeInt(5)},
            {},
        }, diagnostics);
        QVERIFY(diagnostics.isEmpty());
        QVERIFY(called);
        QCOMPARE(seenBackend, QStringLiteral("MathSystem"));
        QCOMPARE(seenSymbol, QStringLiteral("MathSystem.fast_add"));
        QCOMPARE(seenArgCount, static_cast<size_t>(2));
        QCOMPARE(value.asInt(), 9);
    }

    void parsesValidResourceNodeJson()
    {
        const QString text = QStringLiteral(R"({
            "id": "math.backend",
            "kind": "qt_plugin",
            "path": "plugins/libmath_backend.so",
            "iid": "org.abel.IAbelBackend/1.0",
            "backendId": "MathSystem",
            "qtVersion": "6.11.1",
            "kit": "gcc_64",
            "symbols": [
                "MathSystem.fast_add",
                "MathSystem.sort",
                "MathSystem.first_char",
                "MathSystem.char_code",
                "MathSystem.make_range",
                "MathSystem.sum_f64",
                "MathSystem.flip_bools",
                "MathSystem.fail_if_negative"
            ],
            "state": "unloaded",
            "lastError": ""
        })");
        auto parsed = abel::resourceNodeFromJsonText(text, QStringLiteral("<test>"));
        for (const auto& d : parsed.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(parsed.diagnostics.isEmpty());
        QCOMPARE(parsed.node.id, QStringLiteral("math.backend"));
        QCOMPARE(parsed.node.kind, QStringLiteral("qt_plugin"));
        QCOMPARE(parsed.node.backendId, QStringLiteral("MathSystem"));
        QCOMPARE(parsed.node.symbols.size(), 8);
        QCOMPARE(abel::resourceNodeStateName(parsed.node.state), QStringLiteral("unloaded"));
    }

    void rejectsInvalidResourceNodeJson()
    {
        const QString text = QStringLiteral(R"({
            "id": "broken",
            "kind": "file",
            "path": "plugins/libbad.so",
            "iid": "wrong",
            "backendId": "MathSystem",
            "qtVersion": "6.11.1",
            "kit": "gcc_64",
            "symbols": []
        })");
        auto parsed = abel::resourceNodeFromJsonText(text, QStringLiteral("<test>"));
        QVERIFY(!parsed.diagnostics.isEmpty());
    }

    void rejectsResourceNodeSymbolOutsideBackend()
    {
        const QString text = QStringLiteral(R"({
            "id": "bad.symbol",
            "kind": "qt_plugin",
            "path": "plugins/libbad.so",
            "iid": "org.abel.IAbelBackend/1.0",
            "backendId": "MathSystem",
            "qtVersion": "6.11.1",
            "kit": "gcc_64",
            "symbols": ["Other.fast_add"]
        })");
        auto parsed = abel::resourceNodeFromJsonText(text, QStringLiteral("<test>"));
        QVERIFY(!parsed.diagnostics.isEmpty());
    }

    void loadsMathBackendPluginAndCallsRegistry()
    {
        abel::ResourceNode node;
        node.id = QStringLiteral("math.backend");
        node.kind = QStringLiteral("qt_plugin");
        node.path = QStringLiteral("plugins/libmath_backend.so");
        node.iid = QStringLiteral(IAbelBackend_iid);
        node.backendId = QStringLiteral("MathSystem");
        node.qtVersion = QStringLiteral("6.11.1");
        node.kit = QStringLiteral("gcc_64");
        node.symbols = {
            QStringLiteral("MathSystem.fast_add"),
            QStringLiteral("MathSystem.sort"),
            QStringLiteral("MathSystem.first_char"),
            QStringLiteral("MathSystem.char_code"),
            QStringLiteral("MathSystem.make_range"),
            QStringLiteral("MathSystem.sum_f64"),
            QStringLiteral("MathSystem.flip_bools"),
            QStringLiteral("MathSystem.fail_if_negative"),
        };

        abel::BackendRegistry registry;
        auto loaded = abel::loadBackendResourceNode(node, registry, QCoreApplication::applicationDirPath());
        for (const auto& d : loaded.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(loaded.ok());
        QVERIFY(registry.hasFunction(QStringLiteral("MathSystem"), QStringLiteral("fast_add")));
        QVERIFY(registry.hasFunction(QStringLiteral("MathSystem"), QStringLiteral("sort")));
        QVERIFY(registry.hasFunction(QStringLiteral("MathSystem"), QStringLiteral("first_char")));
        QVERIFY(registry.hasFunction(QStringLiteral("MathSystem"), QStringLiteral("char_code")));
        QVERIFY(registry.hasFunction(QStringLiteral("MathSystem"), QStringLiteral("make_range")));
        QVERIFY(registry.hasFunction(QStringLiteral("MathSystem"), QStringLiteral("sum_f64")));
        QVERIFY(registry.hasFunction(QStringLiteral("MathSystem"), QStringLiteral("flip_bools")));
        QVERIFY(registry.hasFunction(QStringLiteral("MathSystem"), QStringLiteral("fail_if_negative")));

        QList<abel::Diagnostic> diagnostics;
        auto sum = registry.call({
            QStringLiteral("MathSystem"),
            QStringLiteral("fast_add"),
            {abel::AbelValue::makeInt(1), abel::AbelValue::makeInt(2)},
            {},
        }, diagnostics);
        for (const auto& d : diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(diagnostics.isEmpty());
        QCOMPARE(sum.asInt(), 3);

        diagnostics.clear();
        auto xs = abel::AbelValue::makeVector(abel::makeType(abel::TypeKind::I32),
                                              {abel::AbelValue::makeInt(3),
                                               abel::AbelValue::makeInt(1),
                                               abel::AbelValue::makeInt(2)});
        auto sorted = registry.call({
            QStringLiteral("MathSystem"),
            QStringLiteral("sort"),
            {xs},
            {},
        }, diagnostics);
        for (const auto& d : diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(diagnostics.isEmpty());
        QCOMPARE(sorted.type().kind, abel::TypeKind::Void);
        QCOMPARE(xs.asVector()->elements[0].asInt(), 1);
        QCOMPARE(xs.asVector()->elements[1].asInt(), 2);
        QCOMPARE(xs.asVector()->elements[2].asInt(), 3);

        diagnostics.clear();
        auto firstChar = registry.call({
            QStringLiteral("MathSystem"),
            QStringLiteral("first_char"),
            {abel::AbelValue::makeString(QStringLiteral("Abel"))},
            {},
        }, diagnostics);
        for (const auto& d : diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(diagnostics.isEmpty());
        QCOMPARE(firstChar.asChar(), QChar('A'));

        diagnostics.clear();
        auto charCode = registry.call({
            QStringLiteral("MathSystem"),
            QStringLiteral("char_code"),
            {abel::AbelValue::makeChar(QChar('A'))},
            {},
        }, diagnostics);
        for (const auto& d : diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(diagnostics.isEmpty());
        QCOMPARE(charCode.asInt(), 65);

        diagnostics.clear();
        auto range = registry.call({
            QStringLiteral("MathSystem"),
            QStringLiteral("make_range"),
            {abel::AbelValue::makeInt(4)},
            {},
        }, diagnostics);
        for (const auto& d : diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(diagnostics.isEmpty());
        QCOMPARE(range.type().kind, abel::TypeKind::Vector);
        QCOMPARE(range.type().pointee->kind, abel::TypeKind::I32);
        QCOMPARE(range.asVector()->elements.size(), static_cast<size_t>(4));
        QCOMPARE(range.asVector()->elements[3].asInt(), 3);

        diagnostics.clear();
        auto f64s = abel::AbelValue::makeVector(abel::makeType(abel::TypeKind::F64),
                                                {abel::AbelValue::makeDouble(1.5),
                                                 abel::AbelValue::makeDouble(2.5)});
        auto f64Sum = registry.call({
            QStringLiteral("MathSystem"),
            QStringLiteral("sum_f64"),
            {f64s},
            {},
        }, diagnostics);
        for (const auto& d : diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(diagnostics.isEmpty());
        QCOMPARE(f64Sum.asDouble(), 4.0);

        diagnostics.clear();
        auto flags = abel::AbelValue::makeVector(abel::makeType(abel::TypeKind::Bool),
                                                 {abel::AbelValue::makeBool(true),
                                                  abel::AbelValue::makeBool(false)});
        auto flipped = registry.call({
            QStringLiteral("MathSystem"),
            QStringLiteral("flip_bools"),
            {flags},
            {},
        }, diagnostics);
        for (const auto& d : diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(diagnostics.isEmpty());
        QCOMPARE(flipped.type().kind, abel::TypeKind::Void);
        QCOMPARE(flags.asVector()->elements[0].asBool(), false);
        QCOMPARE(flags.asVector()->elements[1].asBool(), true);

        diagnostics.clear();
        auto rejected = registry.call({
            QStringLiteral("MathSystem"),
            QStringLiteral("fail_if_negative"),
            {abel::AbelValue::makeInt(-1)},
            {},
        }, diagnostics);
        QCOMPARE(rejected.type().kind, abel::TypeKind::Unknown);
        QCOMPARE(diagnostics.size(), 1);
        QCOMPARE(diagnostics.front().code, QStringLiteral("E0623"));
    }

    void loadedMathBackendRunsThroughInterpreter()
    {
        abel::ResourceNode node;
        node.id = QStringLiteral("math.backend");
        node.kind = QStringLiteral("qt_plugin");
        node.path = QStringLiteral("plugins/libmath_backend.so");
        node.iid = QStringLiteral(IAbelBackend_iid);
        node.backendId = QStringLiteral("MathSystem");
        node.qtVersion = QStringLiteral("6.11.1");
        node.kit = QStringLiteral("gcc_64");
        node.symbols = {
            QStringLiteral("MathSystem.fast_add"),
            QStringLiteral("MathSystem.sort"),
            QStringLiteral("MathSystem.first_char"),
            QStringLiteral("MathSystem.char_code"),
            QStringLiteral("MathSystem.make_range"),
            QStringLiteral("MathSystem.sum_f64"),
            QStringLiteral("MathSystem.flip_bools"),
            QStringLiteral("MathSystem.fail_if_negative"),
        };

        abel::BackendRegistry registry;
        auto loaded = abel::loadBackendResourceNode(node, registry, QCoreApplication::applicationDirPath());
        for (const auto& d : loaded.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(loaded.ok());

        const QString src = QStringLiteral(R"(
            backend MathSystem {
                fn int fast_add(int a, int b);
                fn void sort(vector<int>& xs);
                fn char first_char(str s);
                fn int char_code(char c);
                fn vector<int> make_range(int n);
                fn double sum_f64(vector<double> xs);
                fn void flip_bools(vector<bool>& xs);
                fn int fail_if_negative(int x);
            }

            fn int main() {
                vector<int> xs = {3, 1, 2};
                vector<double> ds = {1.5, 2.5};
                vector<bool> flags = {true, false};
                MathSystem::sort(xs);
                MathSystem::flip_bools(flags);

                int bonus = 0;
                if (flags[0]) {
                    bonus = 100;
                } else {
                    bonus = 10;
                }

                int n = MathSystem::make_range(4).len();
                int c = MathSystem::char_code(MathSystem::first_char("Abel"));
                int d = cast<int>(MathSystem::sum_f64(ds));
                int ok = MathSystem::fail_if_negative(5);
                return MathSystem::fast_add(xs[0], xs[2]) + n + c + d + ok + bonus;
            }
        )");

        abel::Lexer lexer;
        auto lexed = lexer.lex(QStringLiteral("<backend-test>"), src);
        QVERIFY(lexed.diagnostics.isEmpty());

        abel::Parser parser;
        auto parsed = parser.parse(lexed.tokens);
        for (const auto& d : parsed.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(parsed.diagnostics.isEmpty());

        abel::Interpreter interpreter;
        auto result = interpreter.run(*parsed.program, &registry);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 92);
    }
};

QTEST_MAIN(AbelBackendResourceTests)

#include "test_backend_resource.moc"
