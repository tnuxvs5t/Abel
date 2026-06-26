#pragma once

#include "abelcore/runtime.h"

#include <QHash>
#include <QString>
#include <QtGlobal>

#include <utility>

namespace abel {

template <typename T>
class AbelBackendHandleStore {
public:
    AbelBackendHandleStore() = default;

    qint64 create()
    {
        return create(T{});
    }

    qint64 create(T value)
    {
        const qint64 handle = m_nextHandle++;
        m_values.insert(handle, std::move(value));
        return handle;
    }

    template <typename... Args>
    qint64 emplace(Args&&... args)
    {
        return create(T(std::forward<Args>(args)...));
    }

    bool contains(qint64 handle) const
    {
        return m_values.contains(handle);
    }

    T* find(qint64 handle)
    {
        auto it = m_values.find(handle);
        if (it == m_values.end())
            return nullptr;
        return &it.value();
    }

    const T* find(qint64 handle) const
    {
        auto it = m_values.constFind(handle);
        if (it == m_values.constEnd())
            return nullptr;
        return &it.value();
    }

    T* get(qint64 handle,
           AbelRuntimeContext& ctx,
           const QString& label = QStringLiteral("backend handle"),
           const SourceSpan& span = {})
    {
        if (T* value = find(handle))
            return value;
        reportMissing(handle, ctx, label, span);
        return nullptr;
    }

    const T* get(qint64 handle,
                 AbelRuntimeContext& ctx,
                 const QString& label = QStringLiteral("backend handle"),
                 const SourceSpan& span = {}) const
    {
        if (const T* value = find(handle))
            return value;
        reportMissing(handle, ctx, label, span);
        return nullptr;
    }

    bool remove(qint64 handle)
    {
        return m_values.remove(handle) > 0;
    }

    void clear()
    {
        m_values.clear();
    }

    qsizetype size() const
    {
        return m_values.size();
    }

private:
    static void reportMissing(qint64 handle,
                              AbelRuntimeContext& ctx,
                              const QString& label,
                              const SourceSpan& span)
    {
        ctx.error(QStringLiteral("E0630"),
                  QStringLiteral("%1 '%2' does not exist").arg(label).arg(handle),
                  span);
    }

    qint64 m_nextHandle = 1;
    QHash<qint64, T> m_values;
};

} // namespace abel
