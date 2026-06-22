#include "abelcore/backend_registry.h"

namespace abel {

namespace {

QString canonicalSymbol(const QString& backendId, const QString& symbol)
{
    if (symbol.contains(QLatin1Char('.')))
        return symbol;
    return backendId + QStringLiteral(".") + symbol;
}

bool symbolBelongsToBackend(const QString& backendId, const QString& symbol)
{
    if (backendId.isEmpty() || symbol.isEmpty())
        return false;
    if (!symbol.contains(QLatin1Char('.')))
        return true;
    const QString prefix = backendId + QStringLiteral(".");
    return symbol.startsWith(prefix) && symbol.size() > prefix.size();
}

QString shortSymbol(const QString& backendId, const QString& symbol)
{
    const QString prefix = backendId + QStringLiteral(".");
    if (symbol.startsWith(prefix))
        return symbol.mid(prefix.size());
    return symbol;
}

} // namespace

bool BackendRegistry::registerFunction(BackendFunctionDesc desc, Diagnostic* diagnostic)
{
    if (!symbolBelongsToBackend(desc.backendId, desc.symbol)) {
        if (diagnostic) {
            diagnostic->severity = Severity::Error;
            diagnostic->code = QStringLiteral("E0610");
            diagnostic->message = QStringLiteral("backend function registration requires a symbol owned by backend '%1'")
                                      .arg(desc.backendId.isEmpty() ? QStringLiteral("<empty>") : desc.backendId);
        }
        return false;
    }

    const QString backendId = desc.backendId;
    desc.symbol = canonicalSymbol(backendId, desc.symbol);
    const QString symbol = desc.symbol;
    auto& functions = m_backends[backendId];
    if (functions.contains(symbol)) {
        if (diagnostic) {
            diagnostic->severity = Severity::Error;
            diagnostic->code = QStringLiteral("E0611");
            diagnostic->message = QStringLiteral("duplicate backend function '%1::%2'").arg(backendId, shortSymbol(backendId, symbol));
        }
        return false;
    }

    functions.insert(symbol, Entry{std::move(desc), nullptr});
    return true;
}

bool BackendRegistry::bindFunction(const QString& backendId, const QString& symbol, RuntimeCallback runtime, Diagnostic* diagnostic)
{
    if (!runtime) {
        if (diagnostic) {
            diagnostic->severity = Severity::Error;
            diagnostic->code = QStringLiteral("E0610");
            diagnostic->message = QStringLiteral("backend function binding requires a runtime callback");
        }
        return false;
    }

    auto backend = m_backends.find(backendId);
    if (backend == m_backends.end()) {
        if (diagnostic) {
            diagnostic->severity = Severity::Error;
            diagnostic->code = QStringLiteral("E0607");
            diagnostic->message = QStringLiteral("backend '%1' is not registered").arg(backendId);
        }
        return false;
    }
    const QString key = canonicalSymbol(backendId, symbol);
    auto function = backend->find(key);
    if (function == backend->end()) {
        if (diagnostic) {
            diagnostic->severity = Severity::Error;
            diagnostic->code = QStringLiteral("E0608");
            diagnostic->message = QStringLiteral("backend '%1' has no symbol '%2'").arg(backendId, shortSymbol(backendId, key));
        }
        return false;
    }
    function->runtime = std::move(runtime);
    return true;
}

bool BackendRegistry::hasBackend(const QString& backendId) const
{
    return m_backends.contains(backendId);
}

bool BackendRegistry::hasFunction(const QString& backendId, const QString& symbol) const
{
    return findFunction(backendId, symbol) != nullptr;
}

const BackendFunctionDesc* BackendRegistry::findFunction(const QString& backendId, const QString& symbol) const
{
    auto backend = m_backends.constFind(backendId);
    if (backend == m_backends.constEnd())
        return nullptr;
    auto function = backend->constFind(canonicalSymbol(backendId, symbol));
    if (function == backend->constEnd())
        return nullptr;
    return &function->desc;
}

AbelValue BackendRegistry::call(const BackendCall& call, QList<Diagnostic>& diagnostics, AbelRuntimeContext* ctx) const
{
    auto backend = m_backends.constFind(call.backendId);
    const QString key = canonicalSymbol(call.backendId, call.symbol);
    const QString shortName = shortSymbol(call.backendId, key);
    if (backend == m_backends.constEnd()) {
        Diagnostic d;
        d.severity = Severity::Error;
        d.code = QStringLiteral("E0607");
        d.message = QStringLiteral("backend '%1' is not bound; cannot call '%1::%2'").arg(call.backendId, shortName);
        d.primary = call.callSpan;
        diagnostics.push_back(d);
        return AbelValue::makeUnknown();
    }

    auto function = backend->constFind(key);
    if (function == backend->constEnd()) {
        Diagnostic d;
        d.severity = Severity::Error;
        d.code = QStringLiteral("E0608");
        d.message = QStringLiteral("backend '%1' has no symbol '%2'").arg(call.backendId, shortName);
        d.primary = call.callSpan;
        diagnostics.push_back(d);
        return AbelValue::makeUnknown();
    }

    if (!function->runtime) {
        Diagnostic d;
        d.severity = Severity::Error;
        d.code = QStringLiteral("E0607");
        d.message = QStringLiteral("backend '%1' is not bound; cannot call '%1::%2'").arg(call.backendId, shortName);
        d.primary = call.callSpan;
        diagnostics.push_back(d);
        return AbelValue::makeUnknown();
    }

    BackendCall normalized = call;
    normalized.symbol = key;
    if (ctx)
        return function->runtime(normalized, *ctx);

    AbelRuntimeContext localCtx;
    AbelValue result = function->runtime(normalized, localCtx);
    for (const auto& diagnostic : localCtx.diagnostics())
        diagnostics.push_back(diagnostic);
    return result;
}

} // namespace abel
