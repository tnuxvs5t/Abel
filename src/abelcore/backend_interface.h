#pragma once

#include "abelcore/runtime.h"
#include "abelcore/type.h"
#include "abelcore/value.h"

#include <QObject>
#include <QList>
#include <QString>

#include <vector>

namespace abel {

struct AbelBackendFunction {
    QString symbol;
    AbelType returnType;
    std::vector<AbelType> params;
    bool variadic = false;
};

class IAbelBackend {
public:
    virtual ~IAbelBackend() = default;

    virtual QString backendId() const = 0;
    virtual QList<AbelBackendFunction> functions() const = 0;
    virtual AbelValue call(const QString& symbol,
                           QList<AbelValue>& args,
                           AbelRuntimeContext& ctx) = 0;
};

} // namespace abel

#define IAbelBackend_iid "org.abel.IAbelBackend/1.0"
Q_DECLARE_INTERFACE(abel::IAbelBackend, IAbelBackend_iid)
