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

    static ExecResult normal();
    static ExecResult returned(const AbelValue& value);
    static ExecResult breakFlow();
    static ExecResult continueFlow();
};

struct VariableSlot {
    AbelLocation* location = nullptr;
    bool isConst = false;
    bool isReference = false;
};

struct AbelStorage {
    AbelValue value;
};

struct AbelLocation {
    AbelStorage* storage = nullptr;

    AbelValue read() const;
    void write(const AbelValue& value);
};

class AbelRuntimeContext {
public:
    AbelRuntimeContext();

    void pushFrame();
    void popFrame();

    AbelLocation* createStorage(const AbelValue& value);
    bool defineVariable(const QString& name, AbelLocation* location, bool isConst, bool isReference, const SourceSpan& span);
    bool defineValueVariable(const QString& name, const AbelValue& value, bool isConst, const SourceSpan& span);
    VariableSlot* lookupVariable(const QString& name);
    const VariableSlot* lookupVariable(const QString& name) const;
    bool assignVariable(const QString& name, const AbelValue& value, const SourceSpan& span);

    void error(const QString& code, const QString& message, const SourceSpan& span);
    bool hasError() const { return !m_diagnostics.isEmpty(); }
    const QList<Diagnostic>& diagnostics() const { return m_diagnostics; }
    QList<Diagnostic> takeDiagnostics() { return m_diagnostics; }

private:
    QList<QHash<QString, VariableSlot>> m_frames;
    std::vector<std::unique_ptr<AbelStorage>> m_storage;
    std::vector<std::unique_ptr<AbelLocation>> m_locations;
    QList<Diagnostic> m_diagnostics;
};

} // namespace abel
