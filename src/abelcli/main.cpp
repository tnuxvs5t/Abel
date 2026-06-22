#include "abelcore/abel_version.h"
#include "abelcore/lexer.h"
#include "abelcore/parser.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

static void printDiagnostic(const abel::Diagnostic& d)
{
    QTextStream err(stderr);
    err << d.code << ": " << d.message;
    if (!d.primary.file.isEmpty()) {
        err << " at " << d.primary.file << ":" << d.primary.startLine << ":" << d.primary.startColumn;
    }
    err << Qt::endl;
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
    parser.addOption(toolchainOption);
    parser.addPositionalArgument(QStringLiteral("command"),
                                 QStringLiteral("Command: check | run | version"));
    parser.addPositionalArgument(QStringLiteral("input"),
                                 QStringLiteral("Input file or entry."),
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
    if (command == QStringLiteral("check")) {
        if (args.size() < 2) {
            err << "E0003: check expects an input file" << Qt::endl;
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
        out << "ok" << Qt::endl;
        return 0;
    }

    if (command == QStringLiteral("run")) {
        err << "E0001: 'run' is not implemented yet; parser/check is available." << Qt::endl;
        return 2;
    }

    err << "E0002: unknown command '" << command << "'" << Qt::endl;
    return 2;
}
