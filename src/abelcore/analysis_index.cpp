#include "abelcore/analysis_index.h"

#include <QFileInfo>

#include <limits>

namespace abel {

namespace {

bool containsPosition(const SourceSpan& span, const QString& file, int oneBasedLine, int oneBasedColumn)
{
    if (QFileInfo(span.file).absoluteFilePath() != QFileInfo(file).absoluteFilePath())
        return false;
    if (oneBasedLine < span.startLine || oneBasedLine > span.endLine)
        return false;
    if (oneBasedLine == span.startLine && oneBasedColumn < span.startColumn)
        return false;
    if (oneBasedLine == span.endLine && oneBasedColumn > span.endColumn)
        return false;
    return true;
}

int spanScore(const SourceSpan& span)
{
    return (span.endLine - span.startLine) * 100000 + (span.endColumn - span.startColumn);
}

} // namespace

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

const AnalysisBinding* AnalysisIndex::bindingAt(const QString& file, int oneBasedLine, int oneBasedColumn) const
{
    const AnalysisBinding* best = nullptr;
    int bestScore = std::numeric_limits<int>::max();
    for (const AnalysisBinding& binding : m_bindings) {
        if (!containsPosition(binding.use, file, oneBasedLine, oneBasedColumn))
            continue;
        const int score = spanScore(binding.use);
        if (score < bestScore) {
            best = &binding;
            bestScore = score;
        }
    }
    return best;
}

const AnalysisExprInfo* AnalysisIndex::exprInfoAt(const QString& file, int oneBasedLine, int oneBasedColumn) const
{
    const AnalysisExprInfo* best = nullptr;
    int bestScore = std::numeric_limits<int>::max();
    for (const AnalysisExprInfo& info : m_exprInfos) {
        if (!containsPosition(info.span, file, oneBasedLine, oneBasedColumn))
            continue;
        const int score = spanScore(info.span);
        if (score < bestScore) {
            best = &info;
            bestScore = score;
        }
    }
    return best;
}

QList<AnalysisBinding> AnalysisIndex::bindingsForSymbol(AnalysisSymbolId id) const
{
    QList<AnalysisBinding> out;
    for (const AnalysisBinding& binding : m_bindings) {
        if (binding.symbol == id)
            out.push_back(binding);
    }
    return out;
}

} // namespace abel
