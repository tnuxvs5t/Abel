#include "abellsp/lsp_analyzer.h"
#include "abellsp/lsp_protocol.h"
#include "abellsp/lsp_server.h"

#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>

class AbelLspTests final : public QObject {
    Q_OBJECT

private slots:
    void mapsSpanToZeroBasedRange()
    {
        abel::SourceSpan span;
        span.startLine = 2;
        span.startColumn = 3;
        span.endLine = 2;
        span.endColumn = 6;

        const QJsonObject range = abel::lsp::rangeFromSpan(span);
        QCOMPARE(range.value(QStringLiteral("start")).toObject().value(QStringLiteral("line")).toInt(), 1);
        QCOMPARE(range.value(QStringLiteral("start")).toObject().value(QStringLiteral("character")).toInt(), 2);
        QCOMPARE(range.value(QStringLiteral("end")).toObject().value(QStringLiteral("line")).toInt(), 1);
        QCOMPARE(range.value(QStringLiteral("end")).toObject().value(QStringLiteral("character")).toInt(), 5);
    }

    void analyzesOpenSingleFileAndSymbols()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString filePath = QDir(dir.path()).absoluteFilePath(QStringLiteral("main.abel"));

        abel::lsp::Analyzer analyzer;
        QHash<QString, QString> openDocs;
        openDocs.insert(filePath,
                        QStringLiteral("struct Box {\n"
                                       "    int value;\n"
                                       "}\n"
                                       "fn int main() {\n"
                                       "    return \"bad\";\n"
                                       "}\n"));

        const auto result = analyzer.analyzeFile(filePath, openDocs);
        QVERIFY(result.analyzedFiles.contains(filePath));
        QVERIFY(!result.diagnostics.isEmpty());
        QVERIFY(!result.documentSymbols.value(filePath).isEmpty());
        QVERIFY(std::any_of(result.symbols.begin(), result.symbols.end(), [](const abel::lsp::IndexedSymbol& symbol) {
            return symbol.name == QStringLiteral("Box") && symbol.detail == QStringLiteral("struct Box");
        }));

        const QJsonObject hover = analyzer.hover(filePath, 0, 8, openDocs);
        QVERIFY(!hover.isEmpty());
        QVERIFY(hover.value(QStringLiteral("contents")).toObject().value(QStringLiteral("value")).toString().contains(QStringLiteral("struct Box")));
    }

    void analyzesPackageWithOpenOverlay()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir root(dir.path());
        QVERIFY(root.mkdir(QStringLiteral("src")));

        QFile manifest(root.absoluteFilePath(QStringLiteral("abel.package.json")));
        QVERIFY(manifest.open(QIODevice::WriteOnly | QIODevice::Text));
        manifest.write(R"({"name":"demo","version":"0.1.0","entry":"src/main.abel"})");
        manifest.close();

        QFile mainFile(root.absoluteFilePath(QStringLiteral("src/main.abel")));
        QVERIFY(mainFile.open(QIODevice::WriteOnly | QIODevice::Text));
        mainFile.write("module app;\nuse util;\nfn int main() { return inc(1); }\n");
        mainFile.close();

        QFile utilFile(root.absoluteFilePath(QStringLiteral("src/util.abel")));
        QVERIFY(utilFile.open(QIODevice::WriteOnly | QIODevice::Text));
        utilFile.write("module util;\nexport fn int inc(int x) { return x + 1; }\n");
        utilFile.close();

        abel::lsp::Analyzer analyzer;
        const auto result = analyzer.analyzeFile(mainFile.fileName(), {}, root.absolutePath());
        QVERIFY2(result.diagnostics.isEmpty(), qPrintable(result.diagnostics.isEmpty() ? QString() : result.diagnostics.front().message));
        QVERIFY(result.analyzedFiles.contains(QFileInfo(utilFile.fileName()).absoluteFilePath()));

        const QJsonArray definitions = analyzer.definitions(mainFile.fileName(), 2, 24, {}, root.absolutePath());
        QVERIFY(!definitions.isEmpty());
        QCOMPARE(abel::lsp::pathFromUri(definitions.first().toObject().value(QStringLiteral("uri")).toString()),
                 QFileInfo(utilFile.fileName()).absoluteFilePath());

        const QJsonObject hover = analyzer.hover(mainFile.fileName(), 2, 24, {}, root.absolutePath());
        QVERIFY(hover.value(QStringLiteral("contents")).toObject().value(QStringLiteral("value")).toString().contains(QStringLiteral("fn int inc(int x)")));

        const QJsonArray symbols = analyzer.workspaceSymbols(QStringLiteral("inc"), root.absolutePath(), {});
        QVERIFY(std::any_of(symbols.begin(), symbols.end(), [](const QJsonValue& value) {
            return value.toObject().value(QStringLiteral("name")).toString() == QStringLiteral("inc");
        }));
    }

    void initializeAdvertisesSemanticIndexCapabilities()
    {
        abel::lsp::Server server;
        QJsonObject request;
        request.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
        request.insert(QStringLiteral("id"), 1);
        request.insert(QStringLiteral("method"), QStringLiteral("initialize"));
        request.insert(QStringLiteral("params"), QJsonObject());

        const QJsonObject response = server.handleMessage(request);
        const QJsonObject capabilities = response.value(QStringLiteral("result")).toObject()
                                             .value(QStringLiteral("capabilities")).toObject();
        QCOMPARE(capabilities.value(QStringLiteral("hoverProvider")).toBool(), true);
        QCOMPARE(capabilities.value(QStringLiteral("definitionProvider")).toBool(), true);
        QCOMPARE(capabilities.value(QStringLiteral("workspaceSymbolProvider")).toBool(), true);
    }
};

QTEST_MAIN(AbelLspTests)

#include "test_lsp_protocol.moc"
