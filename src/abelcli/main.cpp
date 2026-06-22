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
#include <QTextStream>

#include <vector>

static void printDiagnostic(const abel::Diagnostic& d)
{
    QTextStream err(stderr);
    err << d.code << ": " << d.message;
    if (!d.primary.file.isEmpty()) {
        err << " at " << d.primary.file << ":" << d.primary.startLine << ":" << d.primary.startColumn;
    }
    err << Qt::endl;
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
        }
    }
}

struct CliInput {
    QString sourceFile;
    QString sourceText;
    QString packageRoot;
    QList<abel::ResourceNode> packageResources;
    QList<abel::Diagnostic> diagnostics;
    bool isPackage = false;
};

static CliInput readCliInput(const QString& path)
{
    CliInput input;
    const QFileInfo info(path);
    QString filePath = path;

    if (info.isDir()) {
        input.isPackage = true;
        auto package = abel::packageManifestFromDirectory(path);
        input.diagnostics.append(package.diagnostics);
        if (!package.diagnostics.isEmpty())
            return input;
        input.sourceFile = package.package.entryFilePath();
        input.packageRoot = package.package.rootDir;
        input.packageResources = package.package.backendArtifacts;
        filePath = input.sourceFile;
    } else {
        input.sourceFile = info.absoluteFilePath();
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
                                 QStringLiteral("Command: init | update | check | run | package | resources | version"));
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
        abel::Lexer lexer;
        auto lexed = lexer.lex(input.sourceFile, input.sourceText);
        for (const auto& d : lexed.diagnostics)
            printDiagnostic(d);
        if (!lexed.diagnostics.isEmpty())
            return 1;
        abel::Parser p;
        auto parsed = p.parse(lexed.tokens);
        for (const auto& d : parsed.diagnostics)
            printDiagnostic(d);
        if (!parsed.diagnostics.isEmpty())
            return 1;
        abel::TypeChecker typechecker;
        auto checked = typechecker.check(*parsed.program);
        for (const auto& d : checked.diagnostics)
            printDiagnostic(d);
        if (!checked.diagnostics.isEmpty())
            return 1;
        if (command == QStringLiteral("run")) {
            abel::BackendRegistry backendRegistry;
            std::vector<abel::ResourceNodeLoadResult> loadedResources;
            const QStringList resourceFiles = parser.values(resourceOption);
            loadedResources.reserve(static_cast<size_t>(resourceFiles.size() + input.packageResources.size()));
            for (const auto& resourceNode : input.packageResources) {
                auto loaded = abel::loadBackendResourceNode(resourceNode,
                                                            backendRegistry,
                                                            input.packageRoot);
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
