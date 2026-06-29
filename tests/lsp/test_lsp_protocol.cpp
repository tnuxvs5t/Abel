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

    void indexesLocalVariablesAndReferences()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString filePath = QDir(dir.path()).absoluteFilePath(QStringLiteral("main.abel"));

        abel::lsp::Analyzer analyzer;
        QHash<QString, QString> openDocs;
        openDocs.insert(filePath,
                        QStringLiteral("fn int main() {\n"
                                       "    str name = \"nitori\";\n"
                                       "    int local = 1;\n"
                                       "    return local;\n"
                                       "}\n"));

        const auto result = analyzer.analyzeFile(filePath, openDocs);
        QVERIFY(result.diagnostics.isEmpty());
        QVERIFY(result.analysis);
        QVERIFY(!result.analysis->bindings().isEmpty());
        QVERIFY(std::any_of(result.symbols.begin(), result.symbols.end(), [](const abel::lsp::IndexedSymbol& symbol) {
            return symbol.local && symbol.name == QStringLiteral("local") && symbol.detail == QStringLiteral("int local");
        }));

        const QJsonObject hover = analyzer.hover(filePath, 3, 13, openDocs);
        QVERIFY(hover.value(QStringLiteral("contents")).toObject().value(QStringLiteral("value")).toString().contains(QStringLiteral("int local")));

        const QJsonArray definitions = analyzer.definitions(filePath, 3, 13, openDocs);
        QCOMPARE(definitions.size(), 1);
        QCOMPARE(abel::lsp::pathFromUri(definitions.first().toObject().value(QStringLiteral("uri")).toString()), filePath);
        QCOMPARE(definitions.first().toObject().value(QStringLiteral("range")).toObject()
                     .value(QStringLiteral("start")).toObject().value(QStringLiteral("line")).toInt(),
                 2);

        const QJsonArray refs = analyzer.references(filePath, 3, 13, openDocs);
        QCOMPARE(refs.size(), 2);

        const QJsonArray highlights = analyzer.documentHighlights(filePath, 3, 13, openDocs);
        QCOMPARE(highlights.size(), 2);

        const QJsonArray completions = analyzer.completionItems(filePath, openDocs);
        QVERIFY(std::any_of(completions.begin(), completions.end(), [](const QJsonValue& value) {
            return value.toObject().value(QStringLiteral("label")).toString() == QStringLiteral("local");
        }));

        const QJsonArray folds = analyzer.foldingRanges(filePath, openDocs);
        QVERIFY(!folds.isEmpty());
        QCOMPARE(folds.first().toObject().value(QStringLiteral("startLine")).toInt(), 0);

        const QJsonObject tokens = analyzer.semanticTokens(filePath, openDocs);
        const QJsonArray tokenData = tokens.value(QStringLiteral("data")).toArray();
        QVERIFY(tokenData.size() >= 5);
        QSet<int> tokenTypes;
        for (qsizetype i = 0; i + 3 < tokenData.size(); i += 5)
            tokenTypes.insert(tokenData.at(i + 3).toInt());
        QVERIFY(tokenTypes.contains(1)); // type
        QVERIFY(tokenTypes.contains(3)); // variable
    }

    void supportsDoExpressionLocalsAndSemanticTokens()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString filePath = QDir(dir.path()).absoluteFilePath(QStringLiteral("main.abel"));

        abel::lsp::Analyzer analyzer;
        QHash<QString, QString> openDocs;
        openDocs.insert(filePath,
                        QStringLiteral("fn int main() {\n"
                                       "    int x = 1;\n"
                                       "    int y = do {\n"
                                       "        int flow = x + 2;\n"
                                       "        return flow;\n"
                                       "    };\n"
                                       "    return y;\n"
                                       "}\n"));

        const auto result = analyzer.analyzeFile(filePath, openDocs);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QVERIFY(std::any_of(result.symbols.begin(), result.symbols.end(), [](const abel::lsp::IndexedSymbol& symbol) {
            return symbol.local && symbol.name == QStringLiteral("flow") && symbol.detail == QStringLiteral("int flow");
        }));

        const QJsonObject hover = analyzer.hover(filePath, 4, 16, openDocs);
        QVERIFY(hover.value(QStringLiteral("contents")).toObject().value(QStringLiteral("value")).toString().contains(QStringLiteral("int flow")));

        const QJsonArray definitions = analyzer.definitions(filePath, 4, 16, openDocs);
        QCOMPARE(definitions.size(), 1);
        QCOMPARE(definitions.first().toObject().value(QStringLiteral("range")).toObject()
                     .value(QStringLiteral("start")).toObject().value(QStringLiteral("line")).toInt(),
                 3);

        const QJsonArray completions = analyzer.completionItems(filePath, openDocs);
        QVERIFY(std::any_of(completions.begin(), completions.end(), [](const QJsonValue& value) {
            return value.toObject().value(QStringLiteral("label")).toString() == QStringLiteral("flow");
        }));

        const QJsonObject tokens = analyzer.semanticTokens(filePath, openDocs);
        const QJsonArray tokenData = tokens.value(QStringLiteral("data")).toArray();
        QVERIFY(tokenData.size() >= 5);
        QSet<int> tokenTypes;
        for (qsizetype i = 0; i + 3 < tokenData.size(); i += 5)
            tokenTypes.insert(tokenData.at(i + 3).toInt());
        QVERIFY(tokenTypes.contains(0)); // keyword, including do
        QVERIFY(tokenTypes.contains(3)); // variable
    }

    void completesStructMembersAfterDot()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString filePath = QDir(dir.path()).absoluteFilePath(QStringLiteral("main.abel"));

        abel::lsp::Analyzer analyzer;
        QHash<QString, QString> openDocs;
        openDocs.insert(filePath,
                        QStringLiteral("struct Box {\n"
                                       "    int value;\n"
                                       "    fn int get() {\n"
                                       "        return value;\n"
                                       "    }\n"
                                       "}\n"
                                       "fn int main() {\n"
                                       "    Box b = Box(1);\n"
                                       "    return b.\n"
                                       "}\n"));

        const QJsonArray completions = analyzer.completionItems(filePath, 8, 13, openDocs);
        QVERIFY(std::any_of(completions.begin(), completions.end(), [](const QJsonValue& value) {
            return value.toObject().value(QStringLiteral("label")).toString() == QStringLiteral("value")
                && value.toObject().value(QStringLiteral("kind")).toInt() == 5;
        }));
        QVERIFY(std::any_of(completions.begin(), completions.end(), [](const QJsonValue& value) {
            return value.toObject().value(QStringLiteral("label")).toString() == QStringLiteral("get")
                && value.toObject().value(QStringLiteral("kind")).toInt() == 2;
        }));
    }

    void completesBuiltinMembersAfterDot()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString filePath = QDir(dir.path()).absoluteFilePath(QStringLiteral("main.abel"));

        abel::lsp::Analyzer analyzer;
        QHash<QString, QString> openDocs;
        openDocs.insert(filePath,
                        QStringLiteral("fn int main() {\n"
                                       "    str s = \"abel\";\n"
                                       "    s.\n"
                                       "    return 0;\n"
                                       "}\n"));

        const QJsonArray strCompletions = analyzer.completionItems(filePath, 2, 6, openDocs);
        QVERIFY(std::any_of(strCompletions.begin(), strCompletions.end(), [](const QJsonValue& value) {
            return value.toObject().value(QStringLiteral("label")).toString() == QStringLiteral("trim");
        }));
        QVERIFY(std::any_of(strCompletions.begin(), strCompletions.end(), [](const QJsonValue& value) {
            return value.toObject().value(QStringLiteral("label")).toString() == QStringLiteral("parse_int");
        }));

        openDocs.insert(filePath,
                        QStringLiteral("fn int main() {\n"
                                       "    vector<int> xs = {1, 2};\n"
                                       "    xs.\n"
                                       "    return 0;\n"
                                       "}\n"));
        const QJsonArray vectorCompletions = analyzer.completionItems(filePath, 2, 7, openDocs);
        QVERIFY(std::any_of(vectorCompletions.begin(), vectorCompletions.end(), [](const QJsonValue& value) {
            return value.toObject().value(QStringLiteral("label")).toString() == QStringLiteral("push");
        }));
        QVERIFY(std::any_of(vectorCompletions.begin(), vectorCompletions.end(), [](const QJsonValue& value) {
            return value.toObject().value(QStringLiteral("label")).toString() == QStringLiteral("sort");
        }));
    }

    void renameUsesAnalysisBindingsOnly()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString filePath = QDir(dir.path()).absoluteFilePath(QStringLiteral("main.abel"));

        abel::lsp::Analyzer analyzer;
        QHash<QString, QString> openDocs;
        openDocs.insert(filePath,
                        QStringLiteral("fn int other() {\n"
                                       "    int local = 2;\n"
                                       "    return local;\n"
                                       "}\n"
                                       "fn int main() {\n"
                                       "    int local = 1;\n"
                                       "    return local;\n"
                                       "}\n"));

        const QJsonObject edit = analyzer.rename(filePath, 6, 12, QStringLiteral("renamed"), openDocs);
        const QJsonObject changes = edit.value(QStringLiteral("changes")).toObject();
        const QJsonArray edits = changes.value(abel::lsp::uriFromPath(filePath)).toArray();
        QCOMPARE(edits.size(), 2);
        for (const QJsonValue& value : edits) {
            const QJsonObject item = value.toObject();
            QCOMPARE(item.value(QStringLiteral("newText")).toString(), QStringLiteral("renamed"));
            const int line = item.value(QStringLiteral("range")).toObject()
                                 .value(QStringLiteral("start")).toObject()
                                 .value(QStringLiteral("line")).toInt();
            QVERIFY(line == 5 || line == 6);
        }

        const QJsonObject prepared = analyzer.prepareRename(filePath, 6, 12, openDocs);
        QCOMPARE(prepared.value(QStringLiteral("placeholder")).toString(), QStringLiteral("local"));
        QCOMPARE(prepared.value(QStringLiteral("range")).toObject()
                     .value(QStringLiteral("start")).toObject()
                     .value(QStringLiteral("line")).toInt(),
                 6);
        QVERIFY(analyzer.prepareRename(filePath, 4, 1, openDocs).isEmpty());
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

        const QJsonArray references = analyzer.references(mainFile.fileName(), 2, 24, {}, root.absolutePath());
        QCOMPARE(references.size(), 2);

        const QJsonObject sig = analyzer.signatureHelp(mainFile.fileName(), 2, 28, {}, root.absolutePath());
        QVERIFY(!sig.isEmpty());
        QVERIFY(sig.value(QStringLiteral("signatures")).toArray().first().toObject()
                    .value(QStringLiteral("label")).toString().contains(QStringLiteral("fn int inc(int x)")));

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
        QCOMPARE(capabilities.value(QStringLiteral("referencesProvider")).toBool(), true);
        QCOMPARE(capabilities.value(QStringLiteral("renameProvider")).toObject()
                     .value(QStringLiteral("prepareProvider")).toBool(), true);
        QCOMPARE(capabilities.value(QStringLiteral("documentHighlightProvider")).toBool(), true);
        QCOMPARE(capabilities.value(QStringLiteral("foldingRangeProvider")).toBool(), true);
        QVERIFY(capabilities.value(QStringLiteral("signatureHelpProvider")).toObject()
                    .value(QStringLiteral("triggerCharacters")).toArray().contains(QStringLiteral("(")));
        QVERIFY(capabilities.value(QStringLiteral("semanticTokensProvider")).toObject()
                    .value(QStringLiteral("legend")).toObject()
                    .value(QStringLiteral("tokenTypes")).toArray().contains(QStringLiteral("function")));
    }
};

QTEST_MAIN(AbelLspTests)

#include "test_lsp_protocol.moc"
