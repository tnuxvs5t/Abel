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
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTextStream>

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
        QHash<QString, QString> importedModuleAliases;
        bool importAliasesOk = true;
        for (const auto& decl : parsed.program->declarations) {
            if (auto* module = dynamic_cast<abel::ModuleDeclNode*>(decl.get()))
                moduleName = module->name;
            if (auto* use = dynamic_cast<abel::UseDeclNode*>(decl.get())) {
                importedModules.push_back(use->name);
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

        for (auto& decl : parsed.program->declarations) {
            tagDeclarationPackage(*decl, sourceEntry);
            tagDeclarationModule(*decl, moduleName, importedModules);
            tagDeclarationImportAliases(*decl, importedModuleAliases);
        }
        appendProgram(*result.program, std::move(parsed.program));
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
                                      QStringLiteral("Load a backend ResourceNode JSON before `abel run`."),
                                      QStringLiteral("resource.json"));
    parser.addOption(toolchainOption);
    parser.addOption(resourceOption);
    parser.addPositionalArgument(QStringLiteral("command"),
                                 QStringLiteral("Command: init | add | remove | update | build | check | run | package | resources | version"));
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
            auto parsed = parseSourceFiles(input.sourceFiles);
            for (const auto& d : parsed.diagnostics)
                printDiagnostic(d);
            if (!parsed.diagnostics.isEmpty())
                return 1;
            abel::BackendRegistry backendRegistry;
            std::vector<abel::ResourceNodeLoadResult> loadedResources;
            const QStringList resourceFiles = parser.values(resourceOption);
            loadedResources.reserve(static_cast<size_t>(resourceFiles.size() + input.packageResources.size()));
            for (const auto& packageResource : input.packageResources) {
                auto loaded = abel::loadBackendResourceNode(packageResource.node,
                                                            backendRegistry,
                                                            packageResource.packageRoot);
                for (const auto& d : loaded.diagnostics)
                    printDiagnostic(d);
                if (!loaded.ok())
                    return 1;
                loadedResources.push_back(std::move(loaded));
            }
            for (const QString& resourceFile : resourceFiles) {
                QFile rf(resourceFile);
                if (!rf.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    err << "E0006: cannot open resource '" << resourceFile << "'" << Qt::endl;
                    return 2;
                }
                auto resource = abel::resourceNodeFromJsonText(QString::fromUtf8(rf.readAll()), resourceFile);
                for (const auto& d : resource.diagnostics)
                    printDiagnostic(d);
                if (!resource.diagnostics.isEmpty())
                    return 1;
                auto loaded = abel::loadBackendResourceNode(resource.node,
                                                            backendRegistry,
                                                            QCoreApplication::applicationDirPath());
                for (const auto& d : loaded.diagnostics)
                    printDiagnostic(d);
                if (!loaded.ok())
                    return 1;
                loadedResources.push_back(std::move(loaded));
            }
            abel::Interpreter interpreter;
            const bool hasBackendResources = !resourceFiles.isEmpty() || !input.packageResources.isEmpty();
            auto result = !hasBackendResources
                ? interpreter.run(*parsed.program)
                : interpreter.run(*parsed.program, &backendRegistry);
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
