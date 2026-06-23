#include "abelcore/runtime.h"

namespace abel {

AbelValue AbelLocation::read() const
{
    if (vector)
        return vector->elements[index];
    if (object)
        return object->fields.value(fieldName, AbelValue::makeUnknown());
    return storage ? storage->value : AbelValue::makeUnknown();
}

void AbelLocation::write(const AbelValue& value)
{
    if (vector)
        vector->elements[index] = value;
    else if (object)
        object->fields.insert(fieldName, value);
    else if (storage)
        storage->value = value;
}

ExecResult ExecResult::normal()
{
    return {};
}

ExecResult ExecResult::returned(const AbelValue& value, const SourceSpan& span)
{
    ExecResult r;
    r.kind = FlowKind::Return;
    r.value = value;
    r.span = span;
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

void AbelRuntimeContext::pushFrame(bool boundary, const QString& symbol, const SourceSpan& callSite)
{
    RuntimeFrame frame;
    frame.boundary = boundary;
    frame.symbol = symbol;
    frame.callSite = callSite;
    m_frames.push_back(std::move(frame));
}

void AbelRuntimeContext::popFrame()
{
    if (!m_frames.isEmpty())
        m_frames.pop_back();
}

AbelLocation* AbelRuntimeContext::createStorage(const AbelValue& value)
{
    auto storage = std::make_unique<AbelStorage>();
    storage->value = value;
    AbelStorage* raw = storage.get();
    m_storage.push_back(std::move(storage));
    auto location = std::make_unique<AbelLocation>();
    location->storage = raw;
    AbelLocation* loc = location.get();
    m_locations.push_back(std::move(location));
    return loc;
}

AbelLocation* AbelRuntimeContext::createVectorElementLocation(AbelVectorValue* vector, size_t index)
{
    auto location = std::make_unique<AbelLocation>();
    location->vector = vector;
    location->index = index;
    AbelLocation* loc = location.get();
    m_locations.push_back(std::move(location));
    return loc;
}

AbelLocation* AbelRuntimeContext::createStructFieldLocation(AbelStructValue* object, const QString& fieldName)
{
    auto location = std::make_unique<AbelLocation>();
    location->object = object;
    location->fieldName = fieldName;
    AbelLocation* loc = location.get();
    m_locations.push_back(std::move(location));
    return loc;
}

bool AbelRuntimeContext::defineVariable(const QString& name,
                                        AbelLocation* location,
                                        bool isConst,
                                        bool isReference,
                                        const SourceSpan& span)
{
    if (m_frames.isEmpty())
        pushFrame();
    auto& frame = m_frames.back().variables;
    if (frame.contains(name)) {
        error(QStringLiteral("E0501"), QStringLiteral("variable '%1' is already defined in this scope").arg(name), span);
        return false;
    }
    frame.insert(name, VariableSlot{location, isConst, isReference});
    return true;
}

bool AbelRuntimeContext::defineValueVariable(const QString& name, const AbelValue& value, bool isConst, const SourceSpan& span)
{
    return defineVariable(name, createStorage(value), isConst, false, span);
}

VariableSlot* AbelRuntimeContext::lookupVariable(const QString& name)
{
    for (auto it = m_frames.rbegin(); it != m_frames.rend(); ++it) {
        auto found = it->variables.find(name);
        if (found != it->variables.end())
            return &found.value();
        if (it->boundary)
            break;
    }
    return nullptr;
}

const VariableSlot* AbelRuntimeContext::lookupVariable(const QString& name) const
{
    for (auto it = m_frames.rbegin(); it != m_frames.rend(); ++it) {
        auto found = it->variables.constFind(name);
        if (found != it->variables.constEnd())
            return &found.value();
        if (it->boundary)
            break;
    }
    return nullptr;
}

QHash<QString, VariableSlot> AbelRuntimeContext::visibleVariables() const
{
    QHash<QString, VariableSlot> out;
    qsizetype start = 0;
    for (qsizetype i = m_frames.size() - 1; i >= 0; --i) {
        if (m_frames[i].boundary) {
            start = i;
            break;
        }
    }
    for (qsizetype i = start; i < m_frames.size(); ++i) {
        for (auto it = m_frames[i].variables.constBegin(); it != m_frames[i].variables.constEnd(); ++it)
            out.insert(it.key(), it.value());
    }
    return out;
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
    if (slot->location)
        slot->location->write(value);
    return true;
}

void AbelRuntimeContext::error(const QString& code, const QString& message, const SourceSpan& span)
{
    Diagnostic d;
    d.severity = Severity::Error;
    d.code = code;
    d.message = message;
    d.primary = span;
    for (auto it = m_frames.crbegin(); it != m_frames.crend(); ++it) {
        if (it->symbol.isEmpty())
            continue;
        d.stackTrace.push_back(DiagnosticStackFrame{it->symbol, it->callSite});
    }
    m_diagnostics.push_back(d);
}

RuntimeFrameGuard::RuntimeFrameGuard(AbelRuntimeContext& ctx,
                                     bool boundary,
                                     const QString& symbol,
                                     const SourceSpan& callSite)
    : m_ctx(&ctx)
{
    m_ctx->pushFrame(boundary, symbol, callSite);
}

RuntimeFrameGuard::~RuntimeFrameGuard()
{
    if (m_ctx)
        m_ctx->popFrame();
}

RuntimeFrameGuard::RuntimeFrameGuard(RuntimeFrameGuard&& other) noexcept
    : m_ctx(other.m_ctx)
{
    other.m_ctx = nullptr;
}

RuntimeFrameGuard& RuntimeFrameGuard::operator=(RuntimeFrameGuard&& other) noexcept
{
    if (this == &other)
        return *this;
    if (m_ctx)
        m_ctx->popFrame();
    m_ctx = other.m_ctx;
    other.m_ctx = nullptr;
    return *this;
}

} // namespace abel
