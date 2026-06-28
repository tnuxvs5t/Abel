#include "abelcore/analysis_index.h"

namespace abel {

AnalysisSymbolId AnalysisIndex::addSymbol(AnalysisSymbol symbol)
{
    if (symbol.id <= 0)
        symbol.id = m_nextSymbolId++;
    m_symbolById.insert(symbol.id, m_symbols.size());
    m_symbols.push_back(std::move(symbol));
    return m_symbols.back().id;
}

void AnalysisIndex::addBinding(AnalysisBinding binding)
{
    if (binding.symbol <= 0)
        return;
    m_bindings.push_back(std::move(binding));
}

void AnalysisIndex::addExprInfo(AnalysisExprInfo info)
{
    m_exprInfos.push_back(std::move(info));
}

const AnalysisSymbol* AnalysisIndex::symbolById(AnalysisSymbolId id) const
{
    const auto it = m_symbolById.constFind(id);
    if (it == m_symbolById.constEnd())
        return nullptr;
    const qsizetype index = it.value();
    if (index < 0 || index >= m_symbols.size())
        return nullptr;
    return &m_symbols.at(index);
}

} // namespace abel
