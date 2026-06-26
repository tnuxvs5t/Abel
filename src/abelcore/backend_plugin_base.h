#pragma once

#include "abelcore/backend_binder.h"
#include "abelcore/backend_handle_store.h"

#include <QHash>
#include <QObject>

namespace abel {

class AbelBackendPluginBase : public QObject, public IAbelBackend {
public:
    using QObject::QObject;

    QList<AbelBackendFunction> functions() const override
    {
        return m_functions;
    }

    AbelValue call(const QString& symbol,
                   QList<AbelValue>& args,
                   AbelRuntimeContext& ctx) override
    {
        const QString key = canonicalSymbol(symbol);
        auto found = m_runtime.constFind(key);
        if (found == m_runtime.constEnd()) {
            ctx.error(QStringLiteral("E0622"),
                      QStringLiteral("backend plugin '%1' has no symbol '%2'").arg(backendId(), key),
                      {});
            return AbelValue::makeUnknown();
        }
        return found.value()(args, ctx);
    }

protected:
    template <typename Fn>
    void bind(const QString& symbol, Fn fn)
    {
        const QString key = canonicalSymbol(symbol);
        AbelBackendFunction desc = AbelBackendBinder::describe(key, fn);
        desc.symbol = key;
        m_functions.push_back(desc);
        m_runtime.insert(key, AbelBackendBinder::bind(std::move(fn)));
    }

    template <typename Fn>
    void bindVariadic(const QString& symbol, Fn fn)
    {
        const QString key = canonicalSymbol(symbol);
        AbelBackendFunction desc = AbelBackendBinder::describeVariadic(key, fn);
        desc.symbol = key;
        m_functions.push_back(desc);
        m_runtime.insert(key, AbelBackendBinder::bindVariadic(std::move(fn)));
    }

private:
    QString canonicalSymbol(const QString& symbol) const
    {
        if (symbol.contains(QLatin1Char('.')))
            return symbol;
        return backendId() + QStringLiteral(".") + symbol;
    }

    QList<AbelBackendFunction> m_functions;
    QHash<QString, AbelBackendBinder::Runtime> m_runtime;
};

} // namespace abel
