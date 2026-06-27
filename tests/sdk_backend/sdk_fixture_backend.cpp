#include "abelcore/backend_plugin_base.h"
#include "abelcore/backend_handle_store.h"

#include <QChar>
#include <QHash>

#include <memory>
#include <optional>

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
        bind(QStringLiteral("SdkSystem.call_int"), [](abel::AbelCallable f, int x, abel::AbelRuntimeContext& ctx) {
            return f.call({abel::AbelValue::makeInt(x)}, ctx);
        });
        bind(QStringLiteral("SdkSystem.map_create"), [this]() -> qint64 {
            return m_maps.create();
        });
        bind(QStringLiteral("SdkSystem.map_len"), [this](qint64 h, abel::AbelRuntimeContext& ctx) {
            auto* map = m_maps.get(h, ctx, QStringLiteral("SdkSystem map"));
            return map ? static_cast<int>(map->size()) : 0;
        });
        bind(QStringLiteral("SdkSystem.map_contains"), [this](qint64 h, abel::AbelValue key, abel::AbelRuntimeContext& ctx) {
            auto* map = m_maps.get(h, ctx, QStringLiteral("SdkSystem map"));
            if (!map)
                return false;
            auto keyValue = makeKey(key, ctx);
            if (!keyValue)
                return false;
            return map->contains(*keyValue);
        });
        bind(QStringLiteral("SdkSystem.map_set"), [this](qint64 h, abel::AbelValue key, abel::AbelValue value, abel::AbelRuntimeContext& ctx) {
            auto* map = m_maps.get(h, ctx, QStringLiteral("SdkSystem map"));
            if (!map)
                return;
            auto keyValue = makeKey(key, ctx);
            if (!keyValue)
                return;
            map->insert(*keyValue, value);
        });
        bind(QStringLiteral("SdkSystem.map_get"), [this](qint64 h, abel::AbelValue key, abel::AbelRuntimeContext& ctx) {
            auto* map = m_maps.get(h, ctx, QStringLiteral("SdkSystem map"));
            if (!map)
                return abel::AbelValue::makeUnknown();
            auto keyValue = makeKey(key, ctx);
            if (!keyValue)
                return abel::AbelValue::makeUnknown();
            auto found = map->constFind(*keyValue);
            if (found == map->constEnd()) {
                ctx.error(QStringLiteral("E0632"),
                          QStringLiteral("SdkSystem map key is missing"),
                          {});
                return abel::AbelValue::makeUnknown();
            }
            return found.value();
        });
        bind(QStringLiteral("SdkSystem.dynamic_map"), []() {
            auto store = std::make_shared<QHash<QString, abel::AbelValue>>();
            auto object = std::make_shared<abel::AbelDynamicObject>();
            object->kind = QStringLiteral("sdk.dynamic_map");
            object->get = [store](const abel::AbelValue& key,
                                  abel::AbelRuntimeContext& ctx,
                                  const abel::SourceSpan& span) {
                if (key.type().kind != abel::TypeKind::Str) {
                    ctx.error(QStringLiteral("E0640"),
                              QStringLiteral("sdk.dynamic_map key must be str, got %1").arg(key.type().displayName()),
                              span);
                    return abel::AbelValue::makeUnknown();
                }
                auto found = store->constFind(key.asString());
                if (found == store->constEnd()) {
                    ctx.error(QStringLiteral("E0640"),
                              QStringLiteral("sdk.dynamic_map missing key '%1'").arg(key.asString()),
                              span);
                    return abel::AbelValue::makeUnknown();
                }
                return found.value();
            };
            object->set = [store](const abel::AbelValue& key,
                                  const abel::AbelValue& value,
                                  abel::AbelRuntimeContext& ctx,
                                  const abel::SourceSpan& span) {
                if (key.type().kind != abel::TypeKind::Str) {
                    ctx.error(QStringLiteral("E0641"),
                              QStringLiteral("sdk.dynamic_map key must be str, got %1").arg(key.type().displayName()),
                              span);
                    return;
                }
                store->insert(key.asString(), abel::unboxAny(value));
            };
            object->equals = [object](const abel::AbelValue& other,
                                      abel::AbelRuntimeContext&,
                                      const abel::SourceSpan&) -> std::optional<bool> {
                return other.isDynamicObject() && other.asDynamicObject() == object;
            };
            object->debug = [] {
                return QStringLiteral("<sdk.dynamic_map>");
            };
            return abel::AbelValue::makeDynamicObject(object);
        });
    }

    QString backendId() const override
    {
        return QStringLiteral("SdkSystem");
    }

private:
    using AnyMap = QHash<abel::AbelValueKey, abel::AbelValue>;

    static std::optional<abel::AbelValueKey> makeKey(const abel::AbelValue& key, abel::AbelRuntimeContext& ctx)
    {
        QString error;
        auto out = abel::AbelValueKey::fromValue(key, &error);
        if (!out)
            ctx.error(QStringLiteral("E0631"), error, {});
        return out;
    }

    abel::AbelBackendHandleStore<AnyMap> m_maps;
};

#include "sdk_fixture_backend.moc"
