#include "abelcore/backend_plugin_base.h"

class SdkFixtureBackend final : public abel::AbelBackendPluginBase {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID IAbelBackend_iid)
    Q_INTERFACES(abel::IAbelBackend)

public:
    SdkFixtureBackend()
    {
        bind(QStringLiteral("SdkSystem.add"), [](int a, int b) {
            return a + b;
        });
        bind(QStringLiteral("SdkSystem.echo"), [](QString s) {
            return s;
        });
    }

    QString backendId() const override
    {
        return QStringLiteral("SdkSystem");
    }
};

#include "sdk_fixture_backend.moc"
