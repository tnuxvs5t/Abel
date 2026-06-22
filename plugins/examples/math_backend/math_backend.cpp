#include "abelcore/backend_plugin_base.h"

#include <QChar>

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
        bind(QStringLiteral("MathSystem.first_char"), [](QString s) {
            return s.isEmpty() ? QChar() : s.front();
        });
        bind(QStringLiteral("MathSystem.char_code"), [](QChar c) {
            return static_cast<int>(c.unicode());
        });
        bind(QStringLiteral("MathSystem.make_range"), [](int n) {
            std::vector<int> xs;
            for (int i = 0; i < n; ++i)
                xs.push_back(i);
            return xs;
        });
        bind(QStringLiteral("MathSystem.sum_f64"), [](std::vector<double> xs) {
            double sum = 0.0;
            for (double value : xs)
                sum += value;
            return sum;
        });
        bind(QStringLiteral("MathSystem.flip_bools"), [](std::vector<bool>& xs) {
            for (size_t i = 0; i < xs.size(); ++i)
                xs[i] = !xs[i];
        });
        bind(QStringLiteral("MathSystem.fail_if_negative"), [](int x, abel::AbelRuntimeContext& ctx) {
            if (x < 0) {
                ctx.error(QStringLiteral("E0623"),
                          QStringLiteral("negative value rejected by backend"),
                          {});
                return 0;
            }
            return x;
        });
    }

    QString backendId() const override
    {
        return QStringLiteral("MathSystem");
    }
};

#include "math_backend.moc"
