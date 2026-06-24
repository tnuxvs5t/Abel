#include "abelcore/abel_version.h"
#include "abelcore/interpreter.h"
#include "abelcore/lexer.h"
#include "abelcore/package_manifest.h"
#include "abelcore/parser.h"
#include "abelcore/resource_node.h"
#include "abelcore/typechecker.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>
#include <QXmlStreamWriter>

#include <vector>

static QString caretLineForSpan(const abel::SourceSpan& span)
{
    if (span.startColumn <= 0)
        return {};
    QString caret;
    caret.reserve(qMax(1, span.startColumn));
    for (int i = 1; i < span.startColumn; ++i)
        caret.push_back(QLatin1Char(' '));
    const int width = qMax(1, span.endLine == span.startLine ? span.endColumn - span.startColumn : 1);
    for (int i = 0; i < width; ++i)
        caret.push_back(QLatin1Char('^'));
    return caret;
}

static void printSourceExcerpt(QTextStream& err, const abel::SourceSpan& span, const QString& indent)
{
    if (span.sourceLine.isEmpty())
        return;
    err << indent << span.sourceLine << Qt::endl;
    const QString caret = caretLineForSpan(span);
    if (!caret.isEmpty())
        err << indent << caret << Qt::endl;
}

static void printDiagnostic(const abel::Diagnostic& d)
{
    QTextStream err(stderr);
    err << d.code << ": " << d.message;
    if (!d.primary.file.isEmpty()) {
        err << " at " << d.primary.file << ":" << d.primary.startLine << ":" << d.primary.startColumn;
    }
    err << Qt::endl;
    printSourceExcerpt(err, d.primary, QString());
    if (!d.explanation.isEmpty())
        err << "explanation: " << d.explanation << Qt::endl;
    if (!d.related.isEmpty()) {
        err << "related:" << Qt::endl;
        for (const auto& span : d.related) {
            err << "  at";
            if (!span.file.isEmpty()) {
                err << " " << span.file << ":" << span.startLine << ":" << span.startColumn;
            }
            err << Qt::endl;
            printSourceExcerpt(err, span, QStringLiteral("    "));
        }
    }
    if (!d.suggestions.isEmpty()) {
        err << "suggestions:" << Qt::endl;
        for (const QString& suggestion : d.suggestions)
            err << "  - " << suggestion << Qt::endl;
    }
    if (!d.stackTrace.isEmpty()) {
        err << "stack:" << Qt::endl;
        for (const auto& frame : d.stackTrace) {
            err << "  at " << frame.symbol;
            if (!frame.callSite.file.isEmpty()) {
                err << " (" << frame.callSite.file << ":"
                    << frame.callSite.startLine << ":"
                    << frame.callSite.startColumn << ")";
            }
            err << Qt::endl;
            printSourceExcerpt(err, frame.callSite, QStringLiteral("    "));
        }
    }
}

static QJsonObject sourceSpanToJson(const abel::SourceSpan& span)
{
    QJsonObject object;
    object.insert(QStringLiteral("file"), span.file);
    object.insert(QStringLiteral("line"), span.startLine);
    object.insert(QStringLiteral("column"), span.startColumn);
    object.insert(QStringLiteral("endLine"), span.endLine);
    object.insert(QStringLiteral("endColumn"), span.endColumn);
    if (!span.sourceLine.isEmpty())
        object.insert(QStringLiteral("sourceLine"), span.sourceLine);
    return object;
}

static QJsonObject diagnosticToJson(const abel::Diagnostic& diagnostic)
{
    QJsonObject object;
    object.insert(QStringLiteral("code"), diagnostic.code);
    object.insert(QStringLiteral("message"), diagnostic.message);
    object.insert(QStringLiteral("primary"), sourceSpanToJson(diagnostic.primary));
    if (!diagnostic.explanation.isEmpty())
        object.insert(QStringLiteral("explanation"), diagnostic.explanation);

    QJsonArray related;
    for (const auto& span : diagnostic.related)
        related.push_back(sourceSpanToJson(span));
    object.insert(QStringLiteral("related"), related);

    QJsonArray suggestions;
    for (const QString& suggestion : diagnostic.suggestions)
        suggestions.push_back(suggestion);
    object.insert(QStringLiteral("suggestions"), suggestions);

    QJsonArray stack;
    for (const auto& frame : diagnostic.stackTrace) {
        QJsonObject frameObject;
        frameObject.insert(QStringLiteral("symbol"), frame.symbol);
        frameObject.insert(QStringLiteral("callSite"), sourceSpanToJson(frame.callSite));
        stack.push_back(frameObject);
    }
    object.insert(QStringLiteral("stack"), stack);
    return object;
}

static bool writeJsonReport(const QString& path, const QJsonObject& report, QString* error)
{
    const QFileInfo info(path);
    QDir dir = info.dir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (error)
            *error = QStringLiteral("cannot create report directory '%1'").arg(dir.absolutePath());
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("cannot open report '%1' for writing: %2").arg(path, file.errorString());
        return false;
    }

    const QByteArray json = QJsonDocument(report).toJson(QJsonDocument::Indented);
    if (file.write(json) != json.size()) {
        if (error)
            *error = QStringLiteral("cannot write report '%1': %2").arg(path, file.errorString());
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("cannot commit report '%1': %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

static bool matchesAnySubstring(const QString& text, const QStringList& patterns)
{
    for (const QString& pattern : patterns) {
        if (!pattern.isEmpty() && text.contains(pattern))
            return true;
    }
    return false;
}

static QString junitFailureMessage(const QJsonObject& test)
{
    const QString phase = test.value(QStringLiteral("phase")).toString();
    const QJsonArray diagnostics = test.value(QStringLiteral("diagnostics")).toArray();
    if (!diagnostics.isEmpty()) {
        const QJsonObject first = diagnostics.first().toObject();
        const QString code = first.value(QStringLiteral("code")).toString();
        const QString message = first.value(QStringLiteral("message")).toString();
        if (!code.isEmpty() || !message.isEmpty())
            return QStringLiteral("%1: %2").arg(code, message).trimmed();
    }

    const QJsonValue exitCode = test.value(QStringLiteral("exitCode"));
    if (exitCode.isDouble())
        return QStringLiteral("test failed in %1 phase with exit code %2")
            .arg(phase, QString::number(exitCode.toInt()));
    return QStringLiteral("test failed in %1 phase").arg(phase);
}

static QString junitFailureBody(const QJsonObject& test)
{
    QString body;
    QTextStream stream(&body);
    stream << "test: " << test.value(QStringLiteral("path")).toString() << Qt::endl;
    stream << "phase: " << test.value(QStringLiteral("phase")).toString() << Qt::endl;
    const QJsonValue exitCode = test.value(QStringLiteral("exitCode"));
    if (exitCode.isDouble())
        stream << "exitCode: " << exitCode.toInt() << Qt::endl;

    const QJsonArray diagnostics = test.value(QStringLiteral("diagnostics")).toArray();
    if (!diagnostics.isEmpty()) {
        stream << "diagnostics:" << Qt::endl;
        for (const QJsonValue& value : diagnostics) {
            const QJsonObject diagnostic = value.toObject();
            stream << "- "
                   << diagnostic.value(QStringLiteral("code")).toString()
                   << ": "
                   << diagnostic.value(QStringLiteral("message")).toString()
                   << Qt::endl;
            const QJsonObject primary = diagnostic.value(QStringLiteral("primary")).toObject();
            const QString file = primary.value(QStringLiteral("file")).toString();
            if (!file.isEmpty()) {
                stream << "  at " << file
                       << ":" << primary.value(QStringLiteral("line")).toInt()
                       << ":" << primary.value(QStringLiteral("column")).toInt()
                       << Qt::endl;
            }
            const QString sourceLine = primary.value(QStringLiteral("sourceLine")).toString();
            if (!sourceLine.isEmpty())
                stream << "  " << sourceLine << Qt::endl;
        }
    }
    return body;
}

static bool writeJunitReport(const QString& path, const QJsonObject& report, QString* error)
{
    const QFileInfo info(path);
    QDir dir = info.dir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (error)
            *error = QStringLiteral("cannot create report directory '%1'").arg(dir.absolutePath());
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("cannot open report '%1' for writing: %2").arg(path, file.errorString());
        return false;
    }

    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("testsuite"));
    xml.writeAttribute(QStringLiteral("name"), QStringLiteral("Abel"));
    xml.writeAttribute(QStringLiteral("package"), report.value(QStringLiteral("project")).toString());
    xml.writeAttribute(QStringLiteral("tests"), QString::number(report.value(QStringLiteral("total")).toInt()));
    xml.writeAttribute(QStringLiteral("failures"), QString::number(report.value(QStringLiteral("failed")).toInt()));
    xml.writeAttribute(QStringLiteral("errors"), QStringLiteral("0"));
    xml.writeAttribute(QStringLiteral("skipped"), QString::number(report.value(QStringLiteral("expectedFailures")).toInt()));
    xml.writeAttribute(QStringLiteral("time"), QStringLiteral("0"));

    const QString filter = report.value(QStringLiteral("filter")).toString();
    const QJsonArray expectedFailurePatterns = report.value(QStringLiteral("expectedFailurePatterns")).toArray();
    if (!filter.isEmpty() || !expectedFailurePatterns.isEmpty()) {
        xml.writeStartElement(QStringLiteral("properties"));
        if (!filter.isEmpty()) {
            xml.writeStartElement(QStringLiteral("property"));
            xml.writeAttribute(QStringLiteral("name"), QStringLiteral("filter"));
            xml.writeAttribute(QStringLiteral("value"), filter);
            xml.writeEndElement();
        }
        for (const QJsonValue& value : expectedFailurePatterns) {
            xml.writeStartElement(QStringLiteral("property"));
            xml.writeAttribute(QStringLiteral("name"), QStringLiteral("expectFail"));
            xml.writeAttribute(QStringLiteral("value"), value.toString());
            xml.writeEndElement();
        }
        xml.writeEndElement();
    }

    const QJsonArray tests = report.value(QStringLiteral("tests")).toArray();
    for (const QJsonValue& value : tests) {
        const QJsonObject test = value.toObject();
        xml.writeStartElement(QStringLiteral("testcase"));
        xml.writeAttribute(QStringLiteral("classname"), QStringLiteral("Abel"));
        xml.writeAttribute(QStringLiteral("name"), test.value(QStringLiteral("path")).toString());
        xml.writeAttribute(QStringLiteral("file"), test.value(QStringLiteral("file")).toString());
        xml.writeAttribute(QStringLiteral("time"), QStringLiteral("0"));

        const QString status = test.value(QStringLiteral("status")).toString();
        if (status == QStringLiteral("failed") || status == QStringLiteral("xpass")) {
            xml.writeStartElement(QStringLiteral("failure"));
            xml.writeAttribute(QStringLiteral("type"), status == QStringLiteral("xpass") ? QStringLiteral("xpass") : test.value(QStringLiteral("phase")).toString());
            xml.writeAttribute(QStringLiteral("message"),
                               status == QStringLiteral("xpass")
                                   ? QStringLiteral("expected-fail test unexpectedly passed")
                                   : junitFailureMessage(test));
            xml.writeCharacters(junitFailureBody(test));
            xml.writeEndElement();
        } else if (status == QStringLiteral("xfail")) {
            xml.writeStartElement(QStringLiteral("skipped"));
            xml.writeAttribute(QStringLiteral("message"), QStringLiteral("expected failure"));
            xml.writeCharacters(junitFailureBody(test));
            xml.writeEndElement();
        }

        xml.writeEndElement();
    }

    xml.writeEndElement();
    xml.writeEndDocument();

    if (xml.hasError()) {
        if (error)
            *error = QStringLiteral("cannot write JUnit report '%1'").arg(path);
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("cannot commit report '%1': %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

struct CliInput {
    QString sourceFile;
    QString sourceText;
    QList<abel::PackageSourceFile> sourceFiles;
    QString packageRoot;
    QList<abel::PackageResolvedResource> packageResources;
    QList<abel::Diagnostic> diagnostics;
    bool isPackage = false;
};

static abel::Diagnostic makeCliDiagnostic(const QString& code, const QString& message, const QString& file = {})
{
    abel::Diagnostic d;
    d.severity = abel::Severity::Error;
    d.code = code;
    d.message = message;
    d.primary.file = file;
    return d;
}

static CliInput readCliInput(const QString& path)
{
    CliInput input;
    const QFileInfo info(path);
    QString filePath = path;

    if (info.isDir()) {
        input.isPackage = true;
        auto graph = abel::packageGraphFromDirectory(path);
        input.diagnostics.append(graph.diagnostics);
        if (!graph.ok())
            return input;
        input.sourceFile = graph.root.entryFilePath();
        input.sourceFiles = abel::packageGraphSourceFileEntries(graph);
        input.packageRoot = graph.root.rootDir;
        input.packageResources = abel::cachedPackageBackendArtifacts(graph);
        filePath = input.sourceFile;
    } else {
        input.sourceFile = info.absoluteFilePath();
        abel::PackageSourceFile source;
        source.path = input.sourceFile;
        input.sourceFiles.push_back(source);
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        abel::Diagnostic d;
        d.severity = abel::Severity::Error;
        d.code = QStringLiteral("E0004");
        d.message = QStringLiteral("cannot open '%1'").arg(filePath);
        d.primary.file = filePath;
        input.diagnostics.push_back(d);
        return input;
    }
    input.sourceText = QString::fromUtf8(file.readAll());
    return input;
}

struct ParsedCliProgram {
    std::unique_ptr<abel::ProgramNode> program;
    QList<abel::Diagnostic> diagnostics;
};

struct LoadedCliResources {
    abel::BackendRegistry registry;
    std::vector<abel::ResourceNodeLoadResult> handles;
    QList<abel::Diagnostic> diagnostics;
    bool hasResources = false;
};

struct CliProgramRun {
    int exitCode = 1;
    QList<abel::Diagnostic> diagnostics;
};

struct PendingCliSource {
    abel::PackageSourceFile sourceEntry;
    std::unique_ptr<abel::ProgramNode> program;
    QString moduleName;
    QList<QString> importedModules;
    QList<QString> exportedModules;
    QHash<QString, QString> importedModuleAliases;
};

static void appendProgram(abel::ProgramNode& target, std::unique_ptr<abel::ProgramNode> source)
{
    if (!source)
        return;
    for (auto& decl : source->declarations)
        target.declarations.push_back(std::move(decl));
    if (!target.declarations.empty()) {
        target.span = target.declarations.front()->span;
        const auto& last = target.declarations.back()->span;
        target.span.endOffset = last.endOffset;
        target.span.endLine = last.endLine;
        target.span.endColumn = last.endColumn;
    }
}

static void tagDeclarationPackage(abel::DeclNode& decl, const abel::PackageSourceFile& sourceEntry)
{
    decl.packageName = sourceEntry.packageName;
    decl.fromDependency = sourceEntry.fromDependency;
    if (auto* s = dynamic_cast<abel::StructDeclNode*>(&decl)) {
        for (auto& method : s->methods) {
            method->packageName = sourceEntry.packageName;
            method->fromDependency = sourceEntry.fromDependency;
        }
    }
    if (auto* backend = dynamic_cast<abel::BackendBlockNode*>(&decl)) {
        for (auto& fn : backend->functions) {
            fn->packageName = sourceEntry.packageName;
            fn->fromDependency = sourceEntry.fromDependency;
        }
    }
}

static void tagDeclarationModule(abel::DeclNode& decl, const QString& moduleName, const QList<QString>& importedModules)
{
    decl.moduleName = moduleName;
    decl.importedModules = importedModules;
    if (auto* s = dynamic_cast<abel::StructDeclNode*>(&decl)) {
        for (auto& method : s->methods) {
            method->moduleName = moduleName;
            method->importedModules = importedModules;
        }
    }
    if (auto* backend = dynamic_cast<abel::BackendBlockNode*>(&decl)) {
        for (auto& fn : backend->functions) {
            fn->moduleName = moduleName;
            fn->importedModules = importedModules;
        }
    }
}

static void tagDeclarationImportAliases(abel::DeclNode& decl, const QHash<QString, QString>& importedModuleAliases)
{
    decl.importedModuleAliases = importedModuleAliases;
    if (auto* s = dynamic_cast<abel::StructDeclNode*>(&decl)) {
        for (auto& method : s->methods)
            method->importedModuleAliases = importedModuleAliases;
    }
    if (auto* backend = dynamic_cast<abel::BackendBlockNode*>(&decl)) {
        for (auto& fn : backend->functions)
            fn->importedModuleAliases = importedModuleAliases;
    }
}

static ParsedCliProgram parseSourceFiles(const QList<abel::PackageSourceFile>& sourceFiles)
{
    ParsedCliProgram result;
    result.program = std::make_unique<abel::ProgramNode>();
    QSet<QString> seen;
    std::vector<PendingCliSource> pendingSources;

    for (const abel::PackageSourceFile& sourceEntry : sourceFiles) {
        const QString sourceFile = QFileInfo(sourceEntry.path).absoluteFilePath();
        if (seen.contains(sourceFile))
            continue;
        seen.insert(sourceFile);

        QFile file(sourceFile);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            result.diagnostics.push_back(makeCliDiagnostic(QStringLiteral("E0004"),
                                                           QStringLiteral("cannot open '%1'").arg(sourceFile),
                                                           sourceFile));
            continue;
        }

        abel::Lexer lexer;
        auto lexed = lexer.lex(sourceFile, QString::fromUtf8(file.readAll()));
        result.diagnostics.append(lexed.diagnostics);
        if (!lexed.diagnostics.isEmpty())
            continue;

        abel::Parser parser;
        auto parsed = parser.parse(lexed.tokens);
        result.diagnostics.append(parsed.diagnostics);
        if (!parsed.diagnostics.isEmpty())
            continue;

        QString moduleName;
        QList<QString> importedModules;
        QList<QString> exportedModules;
        QHash<QString, QString> importedModuleAliases;
        bool importAliasesOk = true;
        for (const auto& decl : parsed.program->declarations) {
            if (auto* module = dynamic_cast<abel::ModuleDeclNode*>(decl.get()))
                moduleName = module->name;
            if (auto* use = dynamic_cast<abel::UseDeclNode*>(decl.get())) {
                importedModules.push_back(use->name);
                if (use->exported)
                    exportedModules.push_back(use->name);
                if (!use->alias.isEmpty()) {
                    if (importedModuleAliases.contains(use->alias)) {
                        abel::Diagnostic d;
                        d.severity = abel::Severity::Error;
                        d.code = QStringLiteral("E0208");
                        d.message = QStringLiteral("duplicate import alias '%1'").arg(use->alias);
                        d.primary = use->span;
                        result.diagnostics.push_back(d);
                        importAliasesOk = false;
                    } else {
                        importedModuleAliases.insert(use->alias, use->name);
                    }
                }
            }
        }
        if (!importAliasesOk)
            continue;

        PendingCliSource pending;
        pending.sourceEntry = sourceEntry;
        pending.program = std::move(parsed.program);
        pending.moduleName = moduleName;
        pending.importedModules = importedModules;
        pending.exportedModules = exportedModules;
        pending.importedModuleAliases = importedModuleAliases;
        pendingSources.push_back(std::move(pending));
    }

    QHash<QString, QList<QString>> reexportedModules;
    for (const PendingCliSource& pending : pendingSources) {
        if (pending.moduleName.isEmpty())
            continue;
        auto& targets = reexportedModules[pending.moduleName];
        for (const QString& module : pending.exportedModules) {
            if (!targets.contains(module))
                targets.push_back(module);
        }
    }

    auto expandImports = [&](const QList<QString>& directImports) {
        QList<QString> expanded;
        QSet<QString> seenImports;
        auto addOne = [&](const QString& module, auto&& addRef) -> void {
            if (module.isEmpty() || seenImports.contains(module))
                return;
            seenImports.insert(module);
            expanded.push_back(module);
            for (const QString& reexport : reexportedModules.value(module))
                addRef(reexport, addRef);
        };
        for (const QString& module : directImports)
            addOne(module, addOne);
        return expanded;
    };

    for (PendingCliSource& pending : pendingSources) {
        const QList<QString> expandedImports = expandImports(pending.importedModules);
        for (auto& decl : pending.program->declarations) {
            tagDeclarationPackage(*decl, pending.sourceEntry);
            tagDeclarationModule(*decl, pending.moduleName, expandedImports);
            tagDeclarationImportAliases(*decl, pending.importedModuleAliases);
        }
        appendProgram(*result.program, std::move(pending.program));
    }

    return result;
}

static QList<abel::Diagnostic> checkSourceFiles(const QList<abel::PackageSourceFile>& sourceFiles)
{
    QList<abel::Diagnostic> diagnostics;
    auto parsed = parseSourceFiles(sourceFiles);
    diagnostics.append(parsed.diagnostics);
    if (!diagnostics.isEmpty())
        return diagnostics;
    abel::TypeChecker typechecker;
    auto checked = typechecker.check(*parsed.program);
    diagnostics.append(checked.diagnostics);
    return diagnostics;
}

static LoadedCliResources loadCliResources(const QList<abel::PackageResolvedResource>& packageResources,
                                           const QStringList& resourceFiles,
                                           const QString& explicitResourceBaseDir)
{
    LoadedCliResources result;
    result.hasResources = !packageResources.isEmpty() || !resourceFiles.isEmpty();
    result.handles.reserve(static_cast<size_t>(packageResources.size() + resourceFiles.size()));
    for (const auto& packageResource : packageResources) {
        auto loaded = abel::loadBackendResourceNode(packageResource.node,
                                                    result.registry,
                                                    packageResource.packageRoot);
        result.diagnostics.append(loaded.diagnostics);
        if (loaded.ok())
            result.handles.push_back(std::move(loaded));
    }
    if (!result.diagnostics.isEmpty())
        return result;

    for (const QString& resourceFile : resourceFiles) {
        QFile rf(resourceFile);
        if (!rf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            result.diagnostics.push_back(makeCliDiagnostic(QStringLiteral("E0006"),
                                                           QStringLiteral("cannot open resource '%1'").arg(resourceFile),
                                                           resourceFile));
            continue;
        }
        auto resource = abel::resourceNodeFromJsonText(QString::fromUtf8(rf.readAll()), resourceFile);
        result.diagnostics.append(resource.diagnostics);
        if (!resource.diagnostics.isEmpty())
            continue;
        auto loaded = abel::loadBackendResourceNode(resource.node,
                                                    result.registry,
                                                    explicitResourceBaseDir);
        result.diagnostics.append(loaded.diagnostics);
        if (loaded.ok())
            result.handles.push_back(std::move(loaded));
    }
    return result;
}

static CliProgramRun runSourceFiles(const QList<abel::PackageSourceFile>& sourceFiles,
                                    const QList<abel::PackageResolvedResource>& packageResources,
                                    const QStringList& resourceFiles,
                                    const QString& explicitResourceBaseDir,
                                    bool testFixtureMode = false)
{
    CliProgramRun out;
    auto parsed = parseSourceFiles(sourceFiles);
    out.diagnostics.append(parsed.diagnostics);
    if (!out.diagnostics.isEmpty())
        return out;

    auto loadedResources = loadCliResources(packageResources, resourceFiles, explicitResourceBaseDir);
    out.diagnostics.append(loadedResources.diagnostics);
    if (!out.diagnostics.isEmpty())
        return out;

    abel::Interpreter interpreter;
    auto result = testFixtureMode
        ? (!loadedResources.hasResources
              ? interpreter.runTest(*parsed.program)
              : interpreter.runTest(*parsed.program, &loadedResources.registry))
        : (!loadedResources.hasResources
              ? interpreter.run(*parsed.program)
              : interpreter.run(*parsed.program, &loadedResources.registry));
    out.exitCode = result.exitCode;
    out.diagnostics.append(result.diagnostics);
    return out;
}

static QStringList packageTestFiles(const abel::PackageManifest& root)
{
    QStringList files;
    const QDir testDir(QDir(root.rootDir).absoluteFilePath(QStringLiteral("tests")));
    if (!testDir.exists())
        return files;
    QDirIterator it(testDir.absolutePath(),
                    QStringList{QStringLiteral("*.abel")},
                    QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
        files.push_back(QFileInfo(it.next()).absoluteFilePath());
    files.sort();
    return files;
}

static QList<abel::PackageSourceFile> packageGraphSourceFileEntriesForTest(const abel::PackageGraphResult& graph,
                                                                           const QString& testFile)
{
    QList<abel::PackageSourceFile> entries;
    for (const abel::PackageManifest& dependency : graph.dependencies) {
        for (const QString& path : abel::packageSourceFiles(dependency, false)) {
            abel::PackageSourceFile entry;
            entry.packageName = dependency.name;
            entry.path = path;
            entry.fromDependency = true;
            entry.entry = false;
            entries.push_back(entry);
        }
    }
    for (const QString& path : abel::packageSourceFiles(graph.root, false)) {
        abel::PackageSourceFile entry;
        entry.packageName = graph.root.name;
        entry.path = path;
        entry.fromDependency = false;
        entry.entry = false;
        entries.push_back(entry);
    }

    abel::PackageSourceFile test;
    test.packageName = graph.root.name;
    test.path = QFileInfo(testFile).absoluteFilePath();
    test.fromDependency = false;
    test.entry = true;
    entries.push_back(test);
    return entries;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("abel"));
    QCoreApplication::setApplicationVersion(abel::versionString());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Abel CLI"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption toolchainOption(QStringLiteral("toolchain"),
                                       QStringLiteral("Print the locked Qt/C++ toolchain summary."));
    QCommandLineOption resourceOption({QStringLiteral("resource"), QStringLiteral("r")},
                                      QStringLiteral("Load an extra backend ResourceNode JSON before `abel run` or `abel test`."),
                                      QStringLiteral("resource.json"));
    QCommandLineOption testFilterOption(QStringLiteral("filter"),
                                        QStringLiteral("Only run test files whose relative path contains substring."),
                                        QStringLiteral("substring"));
    QCommandLineOption testReportJsonOption(QStringLiteral("report-json"),
                                            QStringLiteral("Write `abel test` machine-readable JSON report."),
                                            QStringLiteral("report.json"));
    QCommandLineOption testReportJunitOption(QStringLiteral("report-junit"),
                                             QStringLiteral("Write `abel test` JUnit XML report."),
                                             QStringLiteral("report.xml"));
    QCommandLineOption testExpectFailOption(QStringLiteral("expect-fail"),
                                            QStringLiteral("Treat matching test file relative paths as expected failures; unexpected pass fails the run."),
                                            QStringLiteral("substring"));
    parser.addOption(toolchainOption);
    parser.addOption(resourceOption);
    parser.addOption(testFilterOption);
    parser.addOption(testExpectFailOption);
    parser.addOption(testReportJsonOption);
    parser.addOption(testReportJunitOption);
    parser.addPositionalArgument(QStringLiteral("command"),
                                 QStringLiteral("Command: init | add | remove | update | build | test | check | run | package | resources | version"));
    parser.addPositionalArgument(QStringLiteral("input"),
                                 QStringLiteral("Input file, resource subcommand, or entry."),
                                 QStringLiteral("[input]"));

    parser.process(app);

    QTextStream out(stdout);
    QTextStream err(stderr);

    if (parser.isSet(toolchainOption)) {
        out << abel::toolchainString() << Qt::endl;
        return 0;
    }

    const QStringList args = parser.positionalArguments();
    if (args.isEmpty() || args[0] == QStringLiteral("version")) {
        out << abel::versionString() << Qt::endl;
        out << abel::toolchainString() << Qt::endl;
        return 0;
    }

    const QString command = args[0];
    if (command == QStringLiteral("build")) {
        if (args.size() > 2) {
            err << "E0012: build expects: abel build [project-dir]" << Qt::endl;
            return 2;
        }
        const QString projectDir = args.size() == 2 ? args[1] : QStringLiteral(".");
        auto graph = abel::updatePackageGraph(projectDir);
        for (const auto& d : graph.diagnostics)
            printDiagnostic(d);
        if (!graph.ok())
            return 1;

        const QList<abel::Diagnostic> diagnostics = checkSourceFiles(abel::packageGraphSourceFileEntries(graph));
        for (const auto& d : diagnostics)
            printDiagnostic(d);
        if (!diagnostics.isEmpty())
            return 1;

        const auto cache = abel::updatePackageBackendCache(graph);
        for (const auto& d : cache.diagnostics)
            printDiagnostic(d);
        if (!cache.ok())
            return 1;

        out << "built " << graph.root.name << Qt::endl;
        out << "entry " << graph.root.entryFilePath() << Qt::endl;
        out << "wrote " << graph.lockFile << Qt::endl;
        out << "locked " << graph.entries.size() << " package(s)" << Qt::endl;
        out << "cached " << cache.resources.size() << " backend artifact(s)" << Qt::endl;
        return 0;
    }

    if (command == QStringLiteral("add")) {
        if (args.size() < 2) {
            err << "E0010: add expects: abel add path <dependency-dir> [project-dir] "
                   "or abel add registry <package-name> <version-requirement> <registry-dir> [project-dir]"
                << Qt::endl;
            return 2;
        }
        if (args[1] == QStringLiteral("path")) {
            if (args.size() < 3 || args.size() > 4) {
                err << "E0010: add expects: abel add path <dependency-dir> [project-dir]" << Qt::endl;
                return 2;
            }
            const QString dependencyDir = args[2];
            const QString projectDir = args.size() == 4 ? args[3] : QStringLiteral(".");
            auto changed = abel::addPathPackageDependency(projectDir, dependencyDir);
            for (const auto& d : changed.diagnostics)
                printDiagnostic(d);
            if (!changed.ok())
                return 1;
            out << (changed.changed ? "added " : "already present ")
                << changed.dependency.name
                << " path "
                << changed.dependency.path
                << Qt::endl;
            out << "wrote " << changed.manifestFile << Qt::endl;
            out << "wrote " << changed.lockFile << Qt::endl;
            out << "locked " << changed.lockedPackages << " package(s)" << Qt::endl;
            return 0;
        }
        if (args[1] == QStringLiteral("registry")) {
            if (args.size() < 5 || args.size() > 6) {
                err << "E0010: add expects: abel add registry <package-name> <version-requirement> <registry-dir> [project-dir]" << Qt::endl;
                return 2;
            }
            const QString packageName = args[2];
            const QString versionRequirement = args[3];
            const QString registryDir = args[4];
            const QString projectDir = args.size() == 6 ? args[5] : QStringLiteral(".");
            auto changed = abel::addRegistryPackageDependency(projectDir,
                                                              packageName,
                                                              versionRequirement,
                                                              registryDir);
            for (const auto& d : changed.diagnostics)
                printDiagnostic(d);
            if (!changed.ok())
                return 1;
            out << (changed.changed ? "added " : "already present ")
                << changed.dependency.name
                << " registry "
                << changed.dependency.registry
                << " "
                << changed.dependency.version
                << Qt::endl;
            out << "wrote " << changed.manifestFile << Qt::endl;
            out << "wrote " << changed.lockFile << Qt::endl;
            out << "locked " << changed.lockedPackages << " package(s)" << Qt::endl;
            return 0;
        }
        err << "E0010: add expects: abel add path <dependency-dir> [project-dir] "
               "or abel add registry <package-name> <version-requirement> <registry-dir> [project-dir]"
            << Qt::endl;
        return 2;
    }

    if (command == QStringLiteral("remove")) {
        if (args.size() < 2 || args.size() > 3) {
            err << "E0011: remove expects: abel remove <dependency-name> [project-dir]" << Qt::endl;
            return 2;
        }
        const QString dependencyName = args[1];
        const QString projectDir = args.size() == 3 ? args[2] : QStringLiteral(".");
        auto changed = abel::removePackageDependency(projectDir, dependencyName);
        for (const auto& d : changed.diagnostics)
            printDiagnostic(d);
        if (!changed.ok())
            return 1;
        out << "removed " << dependencyName << Qt::endl;
        out << "wrote " << changed.manifestFile << Qt::endl;
        out << "wrote " << changed.lockFile << Qt::endl;
        out << "locked " << changed.lockedPackages << " package(s)" << Qt::endl;
        return 0;
    }

    if (command == QStringLiteral("update")) {
        if (args.size() > 2) {
            err << "E0009: update expects: abel update [project-dir]" << Qt::endl;
            return 2;
        }
        const QString projectDir = args.size() == 2 ? args[1] : QStringLiteral(".");
        auto lock = abel::updatePackageLock(projectDir);
        for (const auto& d : lock.diagnostics)
            printDiagnostic(d);
        if (!lock.ok())
            return 1;
        out << "wrote " << lock.lockFile << Qt::endl;
        out << "locked " << lock.entries.size() << " package(s)" << Qt::endl;
        return 0;
    }

    if (command == QStringLiteral("init")) {
        if (args.size() > 2) {
            err << "E0008: init expects: abel init [project-dir]" << Qt::endl;
            return 2;
        }
        abel::PackageInitOptions options;
        options.rootDir = args.size() == 2 ? args[1] : QStringLiteral(".");
        auto initialized = abel::initPackageProject(options);
        for (const auto& d : initialized.diagnostics)
            printDiagnostic(d);
        if (!initialized.ok())
            return 1;
        out << "created " << initialized.rootDir << Qt::endl;
        for (const QString& file : initialized.createdFiles)
            out << "  " << file << Qt::endl;
        return 0;
    }

    if (command == QStringLiteral("package")) {
        if (args.size() != 3 || args[1] != QStringLiteral("check")) {
            err << "E0007: package expects: abel package check <project-dir>" << Qt::endl;
            return 2;
        }
        auto package = abel::packageManifestFromDirectory(args[2]);
        for (const auto& d : package.diagnostics)
            printDiagnostic(d);
        if (!package.diagnostics.isEmpty())
            return 1;
        out << "ok" << Qt::endl;
        return 0;
    }

    if (command == QStringLiteral("resources")) {
        if (args.size() != 3 || args[1] != QStringLiteral("check")) {
            err << "E0005: resources expects: abel resources check <resource.json>" << Qt::endl;
            return 2;
        }
        QFile file(args[2]);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            err << "E0004: cannot open '" << args[2] << "'" << Qt::endl;
            return 2;
        }
        const QString source = QString::fromUtf8(file.readAll());
        auto parsed = abel::resourceNodeFromJsonText(source, args[2]);
        for (const auto& d : parsed.diagnostics)
            printDiagnostic(d);
        if (!parsed.diagnostics.isEmpty())
            return 1;
        out << "ok" << Qt::endl;
        return 0;
    }

    if (command == QStringLiteral("test")) {
        if (args.size() > 2) {
            err << "E0013: test expects: abel test [--filter substring] [--expect-fail substring] [--report-json file] [--report-junit file] [project-dir]" << Qt::endl;
            return 2;
        }
        const QString projectDir = args.size() == 2 ? args[1] : QStringLiteral(".");
        const QString reportJsonPath = parser.value(testReportJsonOption);
        const QString reportJunitPath = parser.value(testReportJunitOption);
        const QStringList expectedFailurePatterns = parser.values(testExpectFailOption);
        auto graph = abel::packageGraphFromDirectory(projectDir);
        for (const auto& d : graph.diagnostics)
            printDiagnostic(d);
        if (!graph.ok())
            return 1;

        QStringList tests = packageTestFiles(graph.root);
        const QString testFilter = parser.value(testFilterOption);
        if (!testFilter.isEmpty()) {
            QStringList filtered;
            for (const QString& testFile : tests) {
                const QString display = QDir(graph.root.rootDir).relativeFilePath(testFile);
                if (display.contains(testFilter))
                    filtered.push_back(testFile);
            }
            tests = filtered;
        }

        QJsonObject report;
        report.insert(QStringLiteral("project"), graph.root.rootDir);
        report.insert(QStringLiteral("filter"), testFilter);
        QJsonArray expectedFailurePatternValues;
        for (const QString& pattern : expectedFailurePatterns)
            expectedFailurePatternValues.push_back(pattern);
        report.insert(QStringLiteral("expectedFailurePatterns"), expectedFailurePatternValues);
        QJsonArray testReports;

        if (tests.isEmpty()) {
            out << "0/0 test(s) passed" << Qt::endl;
            if (!reportJsonPath.isEmpty()) {
                report.insert(QStringLiteral("total"), 0);
                report.insert(QStringLiteral("passed"), 0);
                report.insert(QStringLiteral("failed"), 0);
                report.insert(QStringLiteral("expectedFailures"), 0);
                report.insert(QStringLiteral("unexpectedFailures"), 0);
                report.insert(QStringLiteral("unexpectedPasses"), 0);
                report.insert(QStringLiteral("tests"), testReports);
                QString error;
                if (!writeJsonReport(reportJsonPath, report, &error)) {
                    err << "E0014: " << error << Qt::endl;
                    return 1;
                }
            }
            if (!reportJunitPath.isEmpty()) {
                report.insert(QStringLiteral("total"), 0);
                report.insert(QStringLiteral("passed"), 0);
                report.insert(QStringLiteral("failed"), 0);
                report.insert(QStringLiteral("expectedFailures"), 0);
                report.insert(QStringLiteral("unexpectedFailures"), 0);
                report.insert(QStringLiteral("unexpectedPasses"), 0);
                report.insert(QStringLiteral("tests"), testReports);
                QString error;
                if (!writeJunitReport(reportJunitPath, report, &error)) {
                    err << "E0014: " << error << Qt::endl;
                    return 1;
                }
            }
            return 0;
        }

        const QList<abel::PackageResolvedResource> packageResources = abel::cachedPackageBackendArtifacts(graph);
        const QStringList resourceFiles = parser.values(resourceOption);
        int passed = 0;
        int failed = 0;
        int expectedFailures = 0;
        int unexpectedFailures = 0;
        int unexpectedPasses = 0;
        for (const QString& testFile : tests) {
            const QString display = QDir(graph.root.rootDir).relativeFilePath(testFile);
            const bool expectFailure = matchesAnySubstring(display, expectedFailurePatterns);
            out << "test " << display << " ... " << Qt::flush;
            QJsonObject testReport;
            testReport.insert(QStringLiteral("path"), display);
            testReport.insert(QStringLiteral("file"), testFile);
            testReport.insert(QStringLiteral("expectedFailure"), expectFailure);
            const QList<abel::PackageSourceFile> sourceFiles = packageGraphSourceFileEntriesForTest(graph, testFile);
            const QList<abel::Diagnostic> checkDiagnostics = checkSourceFiles(sourceFiles);
            if (!checkDiagnostics.isEmpty()) {
                out << (expectFailure ? "xfail" : "FAILED") << Qt::endl;
                for (const auto& d : checkDiagnostics)
                    printDiagnostic(d);
                QJsonArray diagnostics;
                for (const auto& d : checkDiagnostics)
                    diagnostics.push_back(diagnosticToJson(d));
                testReport.insert(QStringLiteral("status"), expectFailure ? QStringLiteral("xfail") : QStringLiteral("failed"));
                testReport.insert(QStringLiteral("phase"), QStringLiteral("check"));
                testReport.insert(QStringLiteral("exitCode"), QJsonValue());
                testReport.insert(QStringLiteral("diagnostics"), diagnostics);
                testReports.push_back(testReport);
                if (expectFailure)
                    ++expectedFailures;
                else {
                    ++failed;
                    ++unexpectedFailures;
                }
                continue;
            }

            const CliProgramRun run = runSourceFiles(sourceFiles,
                                                     packageResources,
                                                     resourceFiles,
                                                     QCoreApplication::applicationDirPath(),
                                                     true);
            if (!run.diagnostics.isEmpty() || run.exitCode != 0) {
                out << (expectFailure ? "xfail" : "FAILED") << Qt::endl;
                for (const auto& d : run.diagnostics)
                    printDiagnostic(d);
                if (run.exitCode != 0)
                    err << "test " << display << " failed with exit code " << run.exitCode << Qt::endl;
                QJsonArray diagnostics;
                for (const auto& d : run.diagnostics)
                    diagnostics.push_back(diagnosticToJson(d));
                testReport.insert(QStringLiteral("status"), expectFailure ? QStringLiteral("xfail") : QStringLiteral("failed"));
                testReport.insert(QStringLiteral("phase"), QStringLiteral("run"));
                testReport.insert(QStringLiteral("exitCode"), run.exitCode);
                testReport.insert(QStringLiteral("diagnostics"), diagnostics);
                testReports.push_back(testReport);
                if (expectFailure)
                    ++expectedFailures;
                else {
                    ++failed;
                    ++unexpectedFailures;
                }
                continue;
            }
            if (expectFailure) {
                out << "XPASS" << Qt::endl;
                testReport.insert(QStringLiteral("status"), QStringLiteral("xpass"));
                ++failed;
                ++unexpectedPasses;
            } else {
                out << "ok" << Qt::endl;
                testReport.insert(QStringLiteral("status"), QStringLiteral("passed"));
                ++passed;
            }
            testReport.insert(QStringLiteral("phase"), QStringLiteral("run"));
            testReport.insert(QStringLiteral("exitCode"), run.exitCode);
            testReport.insert(QStringLiteral("diagnostics"), QJsonArray());
            testReports.push_back(testReport);
        }
        out << passed << "/" << tests.size() << " test(s) passed" << Qt::endl;
        if (expectedFailures > 0 || unexpectedPasses > 0)
            out << expectedFailures << " expected failure(s), "
                << unexpectedPasses << " unexpected pass(es)" << Qt::endl;
        if (!reportJsonPath.isEmpty()) {
            report.insert(QStringLiteral("total"), tests.size());
            report.insert(QStringLiteral("passed"), passed);
            report.insert(QStringLiteral("failed"), failed);
            report.insert(QStringLiteral("expectedFailures"), expectedFailures);
            report.insert(QStringLiteral("unexpectedFailures"), unexpectedFailures);
            report.insert(QStringLiteral("unexpectedPasses"), unexpectedPasses);
            report.insert(QStringLiteral("tests"), testReports);
            QString error;
            if (!writeJsonReport(reportJsonPath, report, &error)) {
                err << "E0014: " << error << Qt::endl;
                return 1;
            }
        }
        if (!reportJunitPath.isEmpty()) {
            report.insert(QStringLiteral("total"), tests.size());
            report.insert(QStringLiteral("passed"), passed);
            report.insert(QStringLiteral("failed"), failed);
            report.insert(QStringLiteral("expectedFailures"), expectedFailures);
            report.insert(QStringLiteral("unexpectedFailures"), unexpectedFailures);
            report.insert(QStringLiteral("unexpectedPasses"), unexpectedPasses);
            report.insert(QStringLiteral("tests"), testReports);
            QString error;
            if (!writeJunitReport(reportJunitPath, report, &error)) {
                err << "E0014: " << error << Qt::endl;
                return 1;
            }
        }
        return failed == 0 ? 0 : 1;
    }

    if (command == QStringLiteral("check") || command == QStringLiteral("run")) {
        if (args.size() < 2) {
            err << "E0003: " << command << " expects an input file or project directory" << Qt::endl;
            return 2;
        }
        CliInput input = readCliInput(args[1]);
        for (const auto& d : input.diagnostics)
            printDiagnostic(d);
        if (!input.diagnostics.isEmpty())
            return 1;
        const QList<abel::Diagnostic> checkDiagnostics = checkSourceFiles(input.sourceFiles);
        for (const auto& d : checkDiagnostics)
            printDiagnostic(d);
        if (!checkDiagnostics.isEmpty())
            return 1;
        if (command == QStringLiteral("run")) {
            const CliProgramRun result = runSourceFiles(input.sourceFiles,
                                                        input.packageResources,
                                                        parser.values(resourceOption),
                                                        QCoreApplication::applicationDirPath());
            for (const auto& d : result.diagnostics)
                printDiagnostic(d);
            return result.exitCode;
        }
        out << "ok" << Qt::endl;
        return 0;
    }

    err << "E0002: unknown command '" << command << "'" << Qt::endl;
    return 2;
}
