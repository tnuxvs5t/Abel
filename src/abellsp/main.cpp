#include "lsp_server.h"

#include <QCoreApplication>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    abel::lsp::Server server;
    return server.run();
}
