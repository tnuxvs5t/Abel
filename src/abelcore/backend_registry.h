#pragma once

#include "abelcore/diagnostic.h"
#include "abelcore/runtime.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

namespace abel {

struct BackendFunctionDesc {
    QString backendId;
    QString symbol;
    AbelType returnType;
    std::vector<AbelType> params;
    bool variadic = false;
};

struct BackendCall {
    QString backendId;
    QString symbol;
    std::vector<AbelValue> args;
    SourceSpan callSpan;
    std::vector<AbelLocation*> argLocations;
};

class BackendRegistry {
public:
    using RuntimeCallback = std::function<AbelValue(const BackendCall&, AbelRuntimeContext&)>;

    bool registerFunction(BackendFunctionDesc desc, Diagnostic* diagnostic = nullptr);
    bool bindFunction(const QString& backendId, const QString& symbol, RuntimeCallback runtime, Diagnostic* diagnostic = nullptr);
    bool hasBackend(const QString& backendId) const;
    bool hasFunction(const QString& backendId, const QString& symbol) const;
    const BackendFunctionDesc* findFunction(const QString& backendId, const QString& symbol) const;

    AbelValue call(const BackendCall& call, QList<Diagnostic>& diagnostics, AbelRuntimeContext* ctx = nullptr) const;

private:
    struct Entry {
        BackendFunctionDesc desc;
        RuntimeCallback runtime;
    };

    QHash<QString, QHash<QString, Entry>> m_backends;
};

} // namespace abel
