#pragma once

#include "abelcore/diagnostic.h"
#include "abelcore/value.h"

#include <QHash>
#include <QList>

#include <memory>
#include <vector>

namespace abel {

enum class FlowKind {
    Normal,
    Return,
    Break,
    Continue,
};

struct ExecResult {
    FlowKind kind = FlowKind::Normal;
    AbelValue value = AbelValue::makeVoid();
    SourceSpan span;

    static ExecResult normal();
    static ExecResult returned(const AbelValue& value, const SourceSpan& span = {});
    static ExecResult breakFlow();
    static ExecResult continueFlow();
};

struct VariableSlot {
    AbelLocation* location = nullptr;
    bool isConst = false;
    bool isReference = false;
};

struct RuntimeFrame {
    QHash<QString, VariableSlot> variables;
    bool boundary = false;
    QString symbol;
    SourceSpan callSite;
};

struct AbelStorage {
    AbelValue value;
};

struct AbelLocation {
    AbelStorage* storage = nullptr;
    AbelVectorValue* vector = nullptr;
    AbelStructValue* object = nullptr;
    AbelType declaredType = makeType(TypeKind::Unknown);
    size_t index = 0;
    QString fieldName;
    bool isReadOnly = false;

    AbelValue read() const;
    void write(const AbelValue& value);
};

class AbelRuntimeContext {
public:
    AbelRuntimeContext();

    void pushFrame(bool boundary = false, const QString& symbol = QString(), const SourceSpan& callSite = {});
    void popFrame();

    AbelLocation* createStorage(const AbelValue& value, bool isReadOnly = false, AbelType declaredType = makeType(TypeKind::Unknown));
    AbelLocation* createVectorElementLocation(AbelVectorValue* vector, size_t index, bool isReadOnly = false);
    AbelLocation* createStructFieldLocation(AbelStructValue* object,
                                            const QString& fieldName,
                                            bool isReadOnly = false,
                                            AbelType declaredType = makeType(TypeKind::Unknown));
    AbelLocation* createAliasLocation(AbelLocation* source, bool isReadOnly);
    bool defineVariable(const QString& name, AbelLocation* location, bool isConst, bool isReference, const SourceSpan& span);
    bool defineValueVariable(const QString& name, const AbelValue& value, bool isConst, const SourceSpan& span);
    VariableSlot* lookupVariable(const QString& name);
    const VariableSlot* lookupVariable(const QString& name) const;
    QHash<QString, VariableSlot> visibleVariables() const;
    bool assignVariable(const QString& name, const AbelValue& value, const SourceSpan& span);

    void error(const QString& code, const QString& message, const SourceSpan& span);
    bool hasError() const { return !m_diagnostics.isEmpty(); }
    const QList<Diagnostic>& diagnostics() const { return m_diagnostics; }
    QList<Diagnostic> takeDiagnostics()
    {
        QList<Diagnostic> out = m_diagnostics;
        m_diagnostics.clear();
        return out;
    }

private:
    QList<RuntimeFrame> m_frames;
    std::vector<std::unique_ptr<AbelStorage>> m_storage;
    std::vector<std::unique_ptr<AbelLocation>> m_locations;
    QList<Diagnostic> m_diagnostics;
};

class RuntimeFrameGuard {
public:
    RuntimeFrameGuard(AbelRuntimeContext& ctx,
                      bool boundary = false,
                      const QString& symbol = QString(),
                      const SourceSpan& callSite = {});
    ~RuntimeFrameGuard();

    RuntimeFrameGuard(const RuntimeFrameGuard&) = delete;
    RuntimeFrameGuard& operator=(const RuntimeFrameGuard&) = delete;

    RuntimeFrameGuard(RuntimeFrameGuard&& other) noexcept;
    RuntimeFrameGuard& operator=(RuntimeFrameGuard&& other) noexcept;

private:
    AbelRuntimeContext* m_ctx = nullptr;
};

} // namespace abel
