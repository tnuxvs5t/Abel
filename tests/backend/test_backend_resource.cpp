#include "abelcore/backend_registry.h"
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
                                      [&called, &seenBackend, &seenSymbol, &seenArgCount](const abel::BackendCall& call) {
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
            "symbols": ["MathSystem.fast_add", "MathSystem.sort"],
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
        QCOMPARE(parsed.node.symbols.size(), 2);
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
};

QTEST_MAIN(AbelBackendResourceTests)

#include "test_backend_resource.moc"
