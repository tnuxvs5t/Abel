#include "abelcore/backend_plugin_base.h"

#include <algorithm>

class MathBackend final : public abel::AbelBackendPluginBase {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID IAbelBackend_iid)
    Q_INTERFACES(abel::IAbelBackend)

public:
    MathBackend()
    {
        bind(QStringLiteral("MathSystem.fast_add"), [](int a, int b) {
            return a + b;
        });
        bind(QStringLiteral("MathSystem.sort"), [](std::vector<int>& xs) {
            std::sort(xs.begin(), xs.end());
        });
    }

    QString backendId() const override
    {
        return QStringLiteral("MathSystem");
    }
};

#include "math_backend.moc"
