#include "abelcore/ast.h"

namespace abel {

QString TypeNode::displayName() const
{
    QString out;
    if (isConst)
        out += QStringLiteral("const ");
    if (elementType)
        out += QStringLiteral("vector<") + elementType->displayName() + QStringLiteral(">");
    else
        out += name;
    for (int i = 0; i < pointerDepth; ++i)
        out += QStringLiteral("*");
    if (isReference)
        out += QStringLiteral("&");
    return out;
}

} // namespace abel
