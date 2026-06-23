#include "abelcore/backend_plugin_base.h"

#include <QChar>

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
        bind(QStringLiteral("SdkSystem.letters"), []() {
            return std::vector<QChar>{QChar('A'), QChar('b')};
        });
        bind(QStringLiteral("SdkSystem.sum_i64"), [](std::vector<qint64> xs) {
            qint64 sum = 0;
            for (qint64 value : xs)
                sum += value;
            return sum;
        });
        bind(QStringLiteral("SdkSystem.guard"), [](int x, abel::AbelRuntimeContext& ctx) {
            if (x < 0) {
                ctx.error(QStringLiteral("E0623"),
                          QStringLiteral("sdk guard rejected negative"),
                          {});
                return 0;
            }
            return x;
        });
        bindVariadic(QStringLiteral("SdkSystem.join"), [](abel::AbelVariadicArgs args) {
            return args.buildString();
        });
        bindVariadic(QStringLiteral("SdkSystem.count"), [](std::vector<abel::AbelValue> args) {
            return static_cast<int>(args.size());
        });
    }

    QString backendId() const override
    {
        return QStringLiteral("SdkSystem");
    }
};

#include "sdk_fixture_backend.moc"
