#include "abelcore/abel_version.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

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
    if (command == QStringLiteral("check") || command == QStringLiteral("run")) {
        err << "E0001: '" << command << "' is not implemented yet; Stage 1 only provides the CLI shell."
            << Qt::endl;
        return 2;
    }

    err << "E0002: unknown command '" << command << "'" << Qt::endl;
    return 2;
}

