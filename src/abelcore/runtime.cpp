#include "abelcore/runtime.h"

namespace abel {

ExecResult ExecResult::normal()
{
    return {};
}

ExecResult ExecResult::returned(const AbelValue& value)
{
    ExecResult r;
    r.kind = FlowKind::Return;
    r.value = value;
    return r;
}

ExecResult ExecResult::breakFlow()
{
    ExecResult r;
    r.kind = FlowKind::Break;
    return r;
}

ExecResult ExecResult::continueFlow()
{
    ExecResult r;
    r.kind = FlowKind::Continue;
    return r;
}

AbelRuntimeContext::AbelRuntimeContext()
{
    pushFrame();
}

void AbelRuntimeContext::pushFrame()
{
    m_frames.push_back({});
}

void AbelRuntimeContext::popFrame()
{
    if (!m_frames.isEmpty())
        m_frames.pop_back();
}

bool AbelRuntimeContext::defineVariable(const QString& name, const AbelValue& value, bool isConst, const SourceSpan& span)
{
    if (m_frames.isEmpty())
        pushFrame();
    auto& frame = m_frames.back();
    if (frame.contains(name)) {
        error(QStringLiteral("E0501"), QStringLiteral("variable '%1' is already defined in this scope").arg(name), span);
        return false;
    }
    frame.insert(name, VariableSlot{value, isConst});
    return true;
}

VariableSlot* AbelRuntimeContext::lookupVariable(const QString& name)
{
    for (auto it = m_frames.rbegin(); it != m_frames.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end())
            return &found.value();
    }
    return nullptr;
}

const VariableSlot* AbelRuntimeContext::lookupVariable(const QString& name) const
{
    for (auto it = m_frames.rbegin(); it != m_frames.rend(); ++it) {
        auto found = it->constFind(name);
        if (found != it->constEnd())
            return &found.value();
    }
    return nullptr;
}

bool AbelRuntimeContext::assignVariable(const QString& name, const AbelValue& value, const SourceSpan& span)
{
    VariableSlot* slot = lookupVariable(name);
    if (!slot) {
        error(QStringLiteral("E0502"), QStringLiteral("unknown variable '%1'").arg(name), span);
        return false;
    }
    if (slot->isConst) {
        error(QStringLiteral("E0503"), QStringLiteral("cannot assign to const variable '%1'").arg(name), span);
        return false;
    }
    slot->value = value;
    return true;
}

void AbelRuntimeContext::error(const QString& code, const QString& message, const SourceSpan& span)
{
    Diagnostic d;
    d.severity = Severity::Error;
    d.code = code;
    d.message = message;
    d.primary = span;
    m_diagnostics.push_back(d);
}

} // namespace abel
