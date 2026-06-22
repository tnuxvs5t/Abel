#include "abelcore/abel_version.h"

#include <QtGlobal>

namespace abel {

QString versionString()
{
    return QStringLiteral("Abel v0.0.1");
}

QString toolchainString()
{
    return QStringLiteral("Qt %1 / C++23").arg(QString::fromLatin1(qVersion()));
}

} // namespace abel

