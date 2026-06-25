#include "abelcore/ast.h"

namespace abel {

QString TypeNode::displayName() const
{
    QString out;
    if (isConst)
        out += QStringLiteral("const ");
    if (elementType)
        out += name == QStringLiteral("func")
            ? QStringLiteral("func ") + elementType->displayName()
            : QStringLiteral("vector<") + elementType->displayName() + QStringLiteral(">");
    else
        out += name;
    if (!typeArguments.empty()) {
        out += QStringLiteral("<");
        for (qsizetype i = 0; i < static_cast<qsizetype>(typeArguments.size()); ++i) {
            if (i > 0)
                out += QStringLiteral(", ");
            out += typeArguments[static_cast<size_t>(i)]->displayName();
        }
        out += QStringLiteral(">");
    }
    if (name == QStringLiteral("func")) {
        out += QStringLiteral("(");
        for (qsizetype i = 0; i < static_cast<qsizetype>(functionParamTypes.size()); ++i) {
            if (i > 0)
                out += QStringLiteral(", ");
            out += functionParamTypes[static_cast<size_t>(i)]->displayName();
        }
        out += QStringLiteral(")");
    }
    for (int i = 0; i < pointerDepth; ++i)
        out += QStringLiteral("*");
    if (isReference)
        out += QStringLiteral("&");
    return out;
}

} // namespace abel
