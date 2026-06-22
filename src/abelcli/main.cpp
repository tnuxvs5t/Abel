#include "abelcore/abel_version.h"
#include "abelcore/interpreter.h"
#include "abelcore/lexer.h"
#include "abelcore/parser.h"
#include "abelcore/resource_node.h"
#include "abelcore/typechecker.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
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

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("abel"));
    QCoreApplication::setApplicationVersion(abel::versionString());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Abel v0 CLI"));
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
                                 QStringLiteral("Command: check | run | resources | version"));
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
            err << "E0003: " << command << " expects an input file" << Qt::endl;
            return 2;
        }
        QFile file(args[1]);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            err << "E0004: cannot open '" << args[1] << "'" << Qt::endl;
            return 2;
        }
        const QString source = QString::fromUtf8(file.readAll());
        abel::Lexer lexer;
        auto lexed = lexer.lex(args[1], source);
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
            loadedResources.reserve(static_cast<size_t>(resourceFiles.size()));
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
            auto result = resourceFiles.isEmpty()
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
