#pragma once

#include "abelcore/source_span.h"
#include "abelcore/type.h"

#include <QHash>
#include <QList>
#include <QString>

namespace abel {

using AnalysisSymbolId = int;

enum class AnalysisSymbolKind {
    Unknown,
    Module,
    Function,
    Struct,
    Field,
    Constructor,
    Method,
    Backend,
    BackendFunction,
    Enum,
    TypeAlias,
    Variable,
    Parameter,
};

enum class AnalysisBindingKind {
    Unknown,
    Variable,
    FunctionValue,
    EnumValue,
    Module,
    Type,
};

enum class AnalysisValueCategory {
    LValue,
    PRValue,
};

struct AnalysisSymbol {
    AnalysisSymbolId id = 0;
    AnalysisSymbolKind kind = AnalysisSymbolKind::Unknown;
    QString name;
    QString detail;
    QString container;
    AbelType type = makeType(TypeKind::Unknown);
    SourceSpan declaration;
    SourceSpan scope;
};

struct AnalysisBinding {
    SourceSpan use;
    AnalysisSymbolId symbol = 0;
    AnalysisBindingKind kind = AnalysisBindingKind::Unknown;
};

struct AnalysisExprInfo {
    SourceSpan span;
    AbelType type = makeType(TypeKind::Unknown);
    AnalysisValueCategory category = AnalysisValueCategory::PRValue;
    bool isMutable = false;
};

class AnalysisIndex {
public:
    AnalysisSymbolId addSymbol(AnalysisSymbol symbol);
    void addBinding(AnalysisBinding binding);
    void addExprInfo(AnalysisExprInfo info);

    const QList<AnalysisSymbol>& symbols() const { return m_symbols; }
    const QList<AnalysisBinding>& bindings() const { return m_bindings; }
    const QList<AnalysisExprInfo>& exprInfos() const { return m_exprInfos; }

    const AnalysisSymbol* symbolById(AnalysisSymbolId id) const;

private:
    AnalysisSymbolId m_nextSymbolId = 1;
    QList<AnalysisSymbol> m_symbols;
    QList<AnalysisBinding> m_bindings;
    QList<AnalysisExprInfo> m_exprInfos;
    QHash<AnalysisSymbolId, qsizetype> m_symbolById;
};

} // namespace abel
