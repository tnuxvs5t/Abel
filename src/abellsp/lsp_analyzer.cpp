#include "lsp_analyzer.h"

#include "abelcore/ast.h"
#include "abelcore/lexer.h"
#include "abelcore/parser.h"
#include "abelcore/typechecker.h"
#include "lsp_protocol.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QSet>

#include <limits>

namespace abel::lsp {
namespace {

struct PendingSource {
    PackageSourceFile sourceEntry;
    std::unique_ptr<ProgramNode> program;
    QString moduleName;
    QList<QString> importedModules;
    QList<QString> exportedModules;
    QHash<QString, QString> importedModuleAliases;
};

struct MemberCompletionContext {
    bool active = false;
    bool needsPatch = false;
    int receiverLine = 0;
    int receiverColumn = 0;
    QString patchedText;
};

bool isIdentChar(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_');
}

MemberCompletionContext memberCompletionContext(const QString& text, int zeroBasedLine, int zeroBasedCharacter)
{
    MemberCompletionContext out;
    const QStringList lines = text.split(QLatin1Char('\n'));
    if (zeroBasedLine < 0 || zeroBasedLine >= lines.size())
        return out;

    const QString line = lines.at(zeroBasedLine);
    const int pos = qBound(0, zeroBasedCharacter, line.size());
    int memberStart = pos;
    while (memberStart > 0 && isIdentChar(line.at(memberStart - 1)))
        --memberStart;

    int accessEnd = memberStart;
    while (accessEnd > 0 && line.at(accessEnd - 1).isSpace())
        --accessEnd;
    int accessStart = -1;
    if (accessEnd > 0 && line.at(accessEnd - 1) == QLatin1Char('.')) {
        accessStart = accessEnd - 1;
    } else if (accessEnd > 1 && line.at(accessEnd - 1) == QLatin1Char('>') && line.at(accessEnd - 2) == QLatin1Char('-')) {
        accessStart = accessEnd - 2;
    }
    if (accessStart < 0)
        return out;

    int receiverEnd = accessStart;
    while (receiverEnd > 0 && line.at(receiverEnd - 1).isSpace())
        --receiverEnd;
    int receiverStart = receiverEnd;
    while (receiverStart > 0 && isIdentChar(line.at(receiverStart - 1)))
        --receiverStart;
    if (receiverStart == receiverEnd)
        return out;

    out.active = true;
    out.receiverLine = zeroBasedLine;
    out.receiverColumn = receiverStart;
    out.needsPatch = memberStart == pos;
    if (out.needsPatch) {
        out.patchedText = text;
        qsizetype offset = 0;
        for (int i = 0; i < zeroBasedLine; ++i)
            offset += lines.at(i).size() + 1;
        offset += pos;
        QString insertion = QStringLiteral("__abel_completion__");
        if (line.mid(pos).trimmed().isEmpty())
            insertion += QLatin1Char(';');
        out.patchedText.insert(offset, insertion);
    }
    return out;
}

void appendProgram(ProgramNode& target, std::unique_ptr<ProgramNode> source)
{
    if (!source)
        return;
    for (auto& decl : source->declarations)
        target.declarations.push_back(std::move(decl));
    if (!target.declarations.empty()) {
        target.span = target.declarations.front()->span;
        const SourceSpan& last = target.declarations.back()->span;
        target.span.endOffset = last.endOffset;
        target.span.endLine = last.endLine;
        target.span.endColumn = last.endColumn;
    }
}

void tagDeclarationPackage(DeclNode& decl, const PackageSourceFile& sourceEntry)
{
    decl.packageName = sourceEntry.packageName;
    decl.fromDependency = sourceEntry.fromDependency;
    if (auto* s = dynamic_cast<StructDeclNode*>(&decl)) {
        for (auto& method : s->methods) {
            method->packageName = sourceEntry.packageName;
            method->fromDependency = sourceEntry.fromDependency;
        }
    }
    if (auto* backend = dynamic_cast<BackendBlockNode*>(&decl)) {
        for (auto& fn : backend->functions) {
            fn->packageName = sourceEntry.packageName;
            fn->fromDependency = sourceEntry.fromDependency;
        }
    }
}

void tagDeclarationModule(DeclNode& decl, const QString& moduleName, const QList<QString>& importedModules)
{
    decl.moduleName = moduleName;
    decl.importedModules = importedModules;
    if (auto* s = dynamic_cast<StructDeclNode*>(&decl)) {
        for (auto& method : s->methods) {
            method->moduleName = moduleName;
            method->importedModules = importedModules;
        }
    }
    if (auto* backend = dynamic_cast<BackendBlockNode*>(&decl)) {
        for (auto& fn : backend->functions) {
            fn->moduleName = moduleName;
            fn->importedModules = importedModules;
        }
    }
}

void tagDeclarationImportAliases(DeclNode& decl, const QHash<QString, QString>& importedModuleAliases)
{
    decl.importedModuleAliases = importedModuleAliases;
    if (auto* s = dynamic_cast<StructDeclNode*>(&decl)) {
        for (auto& method : s->methods)
            method->importedModuleAliases = importedModuleAliases;
    }
    if (auto* backend = dynamic_cast<BackendBlockNode*>(&decl)) {
        for (auto& fn : backend->functions)
            fn->importedModuleAliases = importedModuleAliases;
    }
}

QJsonObject makeSymbol(const QString& name, int kind, const SourceSpan& span, const QJsonArray& children = {})
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("kind"), kind);
    object.insert(QStringLiteral("range"), rangeFromSpan(span));
    object.insert(QStringLiteral("selectionRange"), rangeFromSpan(span));
    if (!children.isEmpty())
        object.insert(QStringLiteral("children"), children);
    return object;
}

QString paramsDisplay(const std::vector<std::unique_ptr<ParameterNode>>& params)
{
    QStringList parts;
    for (const auto& param : params) {
        QString part;
        if (param->variadic)
            part += QStringLiteral("...");
        part += param->type ? param->type->displayName() : QStringLiteral("<unknown>");
        if (!param->name.isEmpty())
            part += QStringLiteral(" ") + param->name;
        if (param->defaultValue)
            part += QStringLiteral(" = ...");
        parts.push_back(part);
    }
    return parts.join(QStringLiteral(", "));
}

QString functionDisplay(const FunctionDeclNode& fn, const QString& prefix = {})
{
    const QString returnType = fn.returnType ? fn.returnType->displayName() : QStringLiteral("void");
    const QString name = fn.isOperator ? QStringLiteral("operator %1").arg(fn.operatorSymbol) : fn.name;
    return QStringLiteral("%1fn %2 %3(%4)")
        .arg(prefix, returnType, name, paramsDisplay(fn.params));
}

QString constructorDisplay(const ConstructorDeclNode& ctor)
{
    return QStringLiteral("init(%1)").arg(paramsDisplay(ctor.params));
}

IndexedSymbol makeIndexed(const QString& name,
                          const QString& detail,
                          int kind,
                          const SourceSpan& span,
                          const QString& container = {},
                          bool local = false,
                          const SourceSpan& scopeRange = {})
{
    IndexedSymbol symbol;
    symbol.name = name;
    symbol.detail = detail;
    symbol.containerName = container;
    symbol.file = QFileInfo(span.file).absoluteFilePath();
    symbol.kind = kind;
    symbol.local = local;
    symbol.range = span;
    symbol.selectionRange = span;
    symbol.scopeRange = scopeRange.file.isEmpty() ? span : scopeRange;
    return symbol;
}

void collectLocalSymbolsFromStmt(QList<IndexedSymbol>& symbols,
                                 const StmtNode& stmt,
                                 const QString& container,
                                 const SourceSpan& scopeRange);

void collectLocalSymbolsFromBlock(QList<IndexedSymbol>& symbols,
                                  const BlockStmtNode* block,
                                  const QString& container,
                                  const SourceSpan& scopeRange)
{
    if (!block)
        return;
    for (const auto& stmt : block->statements)
        collectLocalSymbolsFromStmt(symbols, *stmt, container, scopeRange);
}

void collectLocalSymbolsFromStmt(QList<IndexedSymbol>& symbols,
                                 const StmtNode& stmt,
                                 const QString& container,
                                 const SourceSpan& scopeRange)
{
    if (auto* var = dynamic_cast<const VarDeclStmtNode*>(&stmt)) {
        const QString type = var->type ? var->type->displayName() : QStringLiteral("<unknown>");
        const QString prefix = var->isConst ? QStringLiteral("const ") : QString();
        symbols.push_back(makeIndexed(var->name,
                                      QStringLiteral("%1%2 %3").arg(prefix, type, var->name),
                                      13,
                                      var->span,
                                      container,
                                      true,
                                      scopeRange));
    } else if (auto* block = dynamic_cast<const BlockStmtNode*>(&stmt)) {
        collectLocalSymbolsFromBlock(symbols, block, container, block->span);
    } else if (auto* ifStmt = dynamic_cast<const IfStmtNode*>(&stmt)) {
        for (const auto& branch : ifStmt->branches)
            collectLocalSymbolsFromBlock(symbols, branch.body.get(), container, branch.body ? branch.body->span : scopeRange);
    } else if (auto* whileStmt = dynamic_cast<const WhileStmtNode*>(&stmt)) {
        collectLocalSymbolsFromBlock(symbols, whileStmt->body.get(), container, whileStmt->body ? whileStmt->body->span : scopeRange);
    } else if (auto* repeatStmt = dynamic_cast<const RepeatStmtNode*>(&stmt)) {
        collectLocalSymbolsFromBlock(symbols, repeatStmt->body.get(), container, repeatStmt->body ? repeatStmt->body->span : scopeRange);
    } else if (auto* forStmt = dynamic_cast<const ForStmtNode*>(&stmt)) {
        if (forStmt->init)
            collectLocalSymbolsFromStmt(symbols, *forStmt->init, container, forStmt->span);
        collectLocalSymbolsFromBlock(symbols, forStmt->body.get(), container, forStmt->body ? forStmt->body->span : forStmt->span);
    } else if (auto* rangeFor = dynamic_cast<const RangeForStmtNode*>(&stmt)) {
        symbols.push_back(makeIndexed(rangeFor->variable,
                                      QStringLiteral("<range> %1").arg(rangeFor->variable),
                                      13,
                                      rangeFor->span,
                                      container,
                                      true,
                                      rangeFor->body ? rangeFor->body->span : rangeFor->span));
        collectLocalSymbolsFromBlock(symbols, rangeFor->body.get(), container, rangeFor->body ? rangeFor->body->span : rangeFor->span);
    }
}

void collectParamsAndBody(QList<IndexedSymbol>& symbols,
                          const FunctionDeclNode& fn,
                          const QString& container,
                          const SourceSpan& scopeRange)
{
    for (const auto& param : fn.params) {
        const QString type = param->type ? param->type->displayName() : QStringLiteral("<unknown>");
        const QString variadic = param->variadic ? QStringLiteral("...") : QString();
        symbols.push_back(makeIndexed(param->name,
                                      QStringLiteral("%1%2 %3").arg(variadic, type, param->name),
                                      13,
                                      param->span,
                                      container,
                                      true,
                                      scopeRange));
    }
    collectLocalSymbolsFromBlock(symbols, fn.body.get(), container, fn.body ? fn.body->span : scopeRange);
}

void collectCtorParamsAndBody(QList<IndexedSymbol>& symbols,
                              const ConstructorDeclNode& ctor,
                              const QString& container,
                              const SourceSpan& scopeRange)
{
    for (const auto& param : ctor.params) {
        const QString type = param->type ? param->type->displayName() : QStringLiteral("<unknown>");
        symbols.push_back(makeIndexed(param->name,
                                      QStringLiteral("%1 %2").arg(type, param->name),
                                      13,
                                      param->span,
                                      container,
                                      true,
                                      scopeRange));
    }
    collectLocalSymbolsFromBlock(symbols, ctor.body.get(), container, ctor.body ? ctor.body->span : scopeRange);
}

QList<IndexedSymbol> semanticSymbolsForDecls(const ProgramNode& program)
{
    QList<IndexedSymbol> symbols;
    for (const auto& decl : program.declarations) {
        if (auto* module = dynamic_cast<ModuleDeclNode*>(decl.get())) {
            symbols.push_back(makeIndexed(module->name,
                                          QStringLiteral("module %1").arg(module->name),
                                          2,
                                          module->span));
        } else if (auto* use = dynamic_cast<UseDeclNode*>(decl.get())) {
            QString detail = use->exported ? QStringLiteral("export use ") : QStringLiteral("use ");
            detail += use->name;
            if (!use->alias.isEmpty())
                detail += QStringLiteral(" as ") + use->alias;
            symbols.push_back(makeIndexed(use->alias.isEmpty() ? use->name : use->alias, detail, 3, use->span));
        } else if (auto* fn = dynamic_cast<FunctionDeclNode*>(decl.get())) {
            const QString name = fn->isOperator ? QStringLiteral("operator %1").arg(fn->operatorSymbol) : fn->name;
            symbols.push_back(makeIndexed(name, functionDisplay(*fn), 12, fn->span, fn->moduleName));
            collectParamsAndBody(symbols, *fn, name, fn->span);
        } else if (auto* s = dynamic_cast<StructDeclNode*>(decl.get())) {
            symbols.push_back(makeIndexed(s->name, QStringLiteral("struct %1").arg(s->name), 5, s->span, s->moduleName));
            for (const auto& field : s->fields) {
                const QString type = field->type ? field->type->displayName() : QStringLiteral("<unknown>");
                symbols.push_back(makeIndexed(field->name,
                                              QStringLiteral("%1 %2").arg(type, field->name),
                                              8,
                                              field->span,
                                              s->name));
            }
            for (const auto& ctor : s->constructors) {
                symbols.push_back(makeIndexed(QStringLiteral("init"), constructorDisplay(*ctor), 9, ctor->span, s->name));
                collectCtorParamsAndBody(symbols, *ctor, s->name + QStringLiteral("::init"), ctor->span);
            }
            for (const auto& method : s->methods) {
                symbols.push_back(makeIndexed(method->name, functionDisplay(*method), 6, method->span, s->name));
                collectParamsAndBody(symbols, *method, s->name + QStringLiteral("::") + method->name, method->span);
            }
        } else if (auto* e = dynamic_cast<EnumDeclNode*>(decl.get())) {
            symbols.push_back(makeIndexed(e->name, QStringLiteral("enum %1").arg(e->name), 10, e->span, e->moduleName));
        } else if (auto* alias = dynamic_cast<TypeAliasDeclNode*>(decl.get())) {
            const QString target = alias->targetType ? alias->targetType->displayName() : QStringLiteral("<unknown>");
            symbols.push_back(makeIndexed(alias->name,
                                          QStringLiteral("type %1 = %2").arg(alias->name, target),
                                          13,
                                          alias->span,
                                          alias->moduleName));
        } else if (auto* backend = dynamic_cast<BackendBlockNode*>(decl.get())) {
            symbols.push_back(makeIndexed(backend->name,
                                          QStringLiteral("backend %1").arg(backend->name),
                                          3,
                                          backend->span,
                                          backend->moduleName));
            for (const auto& fn : backend->functions)
                symbols.push_back(makeIndexed(fn->name,
                                              functionDisplay(*fn, backend->name + QStringLiteral("::")),
                                              12,
                                              fn->span,
                                              backend->name));
        }
    }
    return symbols;
}

QJsonArray symbolsForDecls(const ProgramNode& program, const QString& filePath)
{
    QJsonArray symbols;
    const QString abs = QFileInfo(filePath).absoluteFilePath();
    for (const auto& decl : program.declarations) {
        if (QFileInfo(decl->span.file).absoluteFilePath() != abs)
            continue;

        if (auto* module = dynamic_cast<ModuleDeclNode*>(decl.get())) {
            symbols.push_back(makeSymbol(module->name, 2, module->span));
        } else if (auto* use = dynamic_cast<UseDeclNode*>(decl.get())) {
            QString name = use->name;
            if (!use->alias.isEmpty())
                name += QStringLiteral(" as ") + use->alias;
            symbols.push_back(makeSymbol(name, 3, use->span));
        } else if (auto* fn = dynamic_cast<FunctionDeclNode*>(decl.get())) {
            const QString name = fn->isOperator
                ? QStringLiteral("operator %1").arg(fn->operatorSymbol)
                : fn->name;
            symbols.push_back(makeSymbol(name, 12, fn->span));
        } else if (auto* s = dynamic_cast<StructDeclNode*>(decl.get())) {
            QJsonArray children;
            for (const auto& field : s->fields)
                children.push_back(makeSymbol(field->name, 8, field->span));
            for (const auto& ctor : s->constructors)
                children.push_back(makeSymbol(QStringLiteral("init"), 9, ctor->span));
            for (const auto& method : s->methods)
                children.push_back(makeSymbol(method->name, 6, method->span));
            symbols.push_back(makeSymbol(s->name, 5, s->span, children));
        } else if (auto* e = dynamic_cast<EnumDeclNode*>(decl.get())) {
            symbols.push_back(makeSymbol(e->name, 10, e->span));
        } else if (auto* alias = dynamic_cast<TypeAliasDeclNode*>(decl.get())) {
            symbols.push_back(makeSymbol(alias->name, 13, alias->span));
        } else if (auto* backend = dynamic_cast<BackendBlockNode*>(decl.get())) {
            QJsonArray children;
            for (const auto& fn : backend->functions)
                children.push_back(makeSymbol(fn->name, 12, fn->span));
            symbols.push_back(makeSymbol(backend->name, 3, backend->span, children));
        }
    }
    return symbols;
}

bool containsPosition(const SourceSpan& span, const QString& filePath, int zeroBasedLine, int zeroBasedCharacter)
{
    if (QFileInfo(span.file).absoluteFilePath() != QFileInfo(filePath).absoluteFilePath())
        return false;
    const int line = zeroBasedLine + 1;
    const int col = zeroBasedCharacter + 1;
    if (line < span.startLine || line > span.endLine)
        return false;
    if (line == span.startLine && col < span.startColumn)
        return false;
    if (line == span.endLine && col > span.endColumn)
        return false;
    return true;
}

QJsonObject locationFromSymbol(const IndexedSymbol& symbol)
{
    QJsonObject location;
    location.insert(QStringLiteral("uri"), uriFromPath(symbol.file));
    location.insert(QStringLiteral("range"), rangeFromSpan(symbol.selectionRange));
    return location;
}

QJsonObject locationFromAnalysisSymbol(const AnalysisSymbol& symbol)
{
    QJsonObject location;
    location.insert(QStringLiteral("uri"), uriFromPath(symbol.declaration.file));
    location.insert(QStringLiteral("range"), rangeFromSpan(symbol.declaration));
    return location;
}

QJsonObject locationFromAnalysisBinding(const AnalysisBinding& binding)
{
    QJsonObject location;
    location.insert(QStringLiteral("uri"), uriFromPath(binding.use.file));
    location.insert(QStringLiteral("range"), rangeFromSpan(binding.use));
    return location;
}

QString valueCategoryName(AnalysisValueCategory category)
{
    return category == AnalysisValueCategory::LValue
        ? QStringLiteral("lvalue")
        : QStringLiteral("prvalue");
}

QString analysisSymbolDetail(const AnalysisSymbol& symbol)
{
    if (!symbol.detail.isEmpty() && symbol.detail != QStringLiteral("<unknown>"))
        return symbol.detail;
    if (symbol.type.kind != TypeKind::Unknown)
        return symbol.type.displayName();
    return symbol.name;
}

QString analysisSymbolHoverLine(const AnalysisSymbol& symbol)
{
    const QString detail = analysisSymbolDetail(symbol);
    switch (symbol.kind) {
    case AnalysisSymbolKind::Function:
    case AnalysisSymbolKind::Method:
    case AnalysisSymbolKind::Constructor:
    case AnalysisSymbolKind::Backend:
    case AnalysisSymbolKind::BackendFunction:
    case AnalysisSymbolKind::Module:
    case AnalysisSymbolKind::Struct:
    case AnalysisSymbolKind::Enum:
    case AnalysisSymbolKind::TypeAlias:
        return detail;
    case AnalysisSymbolKind::Field:
    case AnalysisSymbolKind::Variable:
    case AnalysisSymbolKind::Parameter:
        return QStringLiteral("%1 %2").arg(detail, symbol.name);
    case AnalysisSymbolKind::Unknown:
        return detail;
    }
    return detail;
}

QJsonObject workspaceSymbolFromIndexed(const IndexedSymbol& symbol)
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), symbol.name);
    object.insert(QStringLiteral("kind"), symbol.kind);
    if (!symbol.containerName.isEmpty())
        object.insert(QStringLiteral("containerName"), symbol.containerName);
    object.insert(QStringLiteral("location"), locationFromSymbol(symbol));
    return object;
}

int completionKindForSymbol(const IndexedSymbol& symbol)
{
    if (symbol.local)
        return 6;
    switch (symbol.kind) {
    case 2:
        return 9;  // Module
    case 5:
        return 22; // Struct
    case 6:
        return 2;  // Method
    case 8:
        return 5;  // Field
    case 9:
        return 4;  // Constructor
    case 10:
        return 13; // Enum
    case 12:
        return 3;  // Function
    case 13:
        return 25; // Type parameter / alias-ish
    default:
        return 6;
    }
}

QJsonObject completionItemFromSymbol(const IndexedSymbol& symbol)
{
    QJsonObject item;
    item.insert(QStringLiteral("label"), symbol.name);
    item.insert(QStringLiteral("kind"), completionKindForSymbol(symbol));
    if (!symbol.detail.isEmpty())
        item.insert(QStringLiteral("detail"), symbol.detail);
    if (!symbol.containerName.isEmpty())
        item.insert(QStringLiteral("documentation"), QStringLiteral("container: %1").arg(symbol.containerName));
    return item;
}

int semanticKindForToken(const Token& token, const QList<IndexedSymbol>& symbols)
{
    static const QSet<QString> builtinTypes = {
        QStringLiteral("void"),
        QStringLiteral("bool"),
        QStringLiteral("int"),
        QStringLiteral("i32"),
        QStringLiteral("long"),
        QStringLiteral("ll"),
        QStringLiteral("i64"),
        QStringLiteral("double"),
        QStringLiteral("f64"),
        QStringLiteral("char"),
        QStringLiteral("str"),
        QStringLiteral("any"),
        QStringLiteral("vector"),
        QStringLiteral("func"),
    };

    switch (token.kind) {
    case TokenKind::String:
    case TokenKind::Char:
        return 5; // string
    case TokenKind::Integer:
    case TokenKind::Float:
        return 6; // number
    case TokenKind::KwAny:
    case TokenKind::KwVector:
    case TokenKind::KwFunc:
        return 1; // type
    case TokenKind::Identifier:
        if (builtinTypes.contains(token.text))
            return 1; // type
        for (const IndexedSymbol& symbol : symbols) {
            if (symbol.name != token.text)
                continue;
            if (symbol.local)
                return 3; // variable
            if (symbol.kind == 5 || symbol.kind == 10 || symbol.kind == 13)
                return 1; // type
            if (symbol.kind == 12 || symbol.kind == 6 || symbol.kind == 9)
                return 2; // function
            if (symbol.kind == 8)
                return 4; // property
        }
        return 3; // variable
    case TokenKind::Plus:
    case TokenKind::Minus:
    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::Percent:
    case TokenKind::Amp:
    case TokenKind::Pipe:
    case TokenKind::Bang:
    case TokenKind::Equal:
    case TokenKind::EqualEqual:
    case TokenKind::BangEqual:
    case TokenKind::Less:
    case TokenKind::LessEqual:
    case TokenKind::Greater:
    case TokenKind::GreaterEqual:
    case TokenKind::AndAnd:
    case TokenKind::OrOr:
    case TokenKind::Power:
    case TokenKind::ModMod:
    case TokenKind::MinOp:
    case TokenKind::MaxOp:
    case TokenKind::PipeForward:
    case TokenKind::Ellipsis:
        return 7; // operator
    default:
        break;
    }

    if (token.kind >= TokenKind::KwFn)
        return 0; // keyword
    return -1;
}

const IndexedSymbol* bestSymbolForToken(const QList<IndexedSymbol>& symbols,
                                        const QString& token,
                                        const QString& filePath,
                                        int zeroBasedLine,
                                        int zeroBasedCharacter)
{
    if (token.isEmpty())
        return nullptr;

    const IndexedSymbol* bestLocal = nullptr;
    int bestLocalSpan = std::numeric_limits<int>::max();
    for (const IndexedSymbol& symbol : symbols) {
        if (!symbol.local || symbol.name != token)
            continue;
        if (!containsPosition(symbol.scopeRange, filePath, zeroBasedLine, zeroBasedCharacter))
            continue;
        const int spanSize = (symbol.scopeRange.endLine - symbol.scopeRange.startLine) * 100000
            + (symbol.scopeRange.endColumn - symbol.scopeRange.startColumn);
        if (spanSize < bestLocalSpan) {
            bestLocal = &symbol;
            bestLocalSpan = spanSize;
        }
    }
    if (bestLocal)
        return bestLocal;

    for (const IndexedSymbol& symbol : symbols) {
        if (symbol.name == token && !symbol.local)
            return &symbol;
    }
    for (const IndexedSymbol& symbol : symbols) {
        if (symbol.name == token)
            return &symbol;
    }
    return nullptr;
}

bool tokenBelongsToSelectedSymbol(const IndexedSymbol* selected,
                                  const QString& token,
                                  const SourceSpan& tokenSpan)
{
    if (!selected)
        return false;
    if (selected->name != token)
        return false;
    if (!selected->local)
        return true;
    return containsPosition(selected->scopeRange,
                            tokenSpan.file,
                            tokenSpan.startLine - 1,
                            tokenSpan.startColumn - 1);
}

QList<SourceSpan> tokenSpansInFile(const QString& filePath,
                                   const QString& token,
                                   const QHash<QString, QString>& openDocuments,
                                   QList<Diagnostic>& diagnostics)
{
    QList<SourceSpan> out;
    if (token.isEmpty())
        return out;

    const QString abs = QFileInfo(filePath).absoluteFilePath();
    QString text;
    if (openDocuments.contains(abs)) {
        text = openDocuments.value(abs);
    } else {
        QFile file(abs);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            Diagnostic d;
            d.severity = Severity::Error;
            d.code = QStringLiteral("E0004");
            d.message = QStringLiteral("cannot open '%1'").arg(abs);
            d.primary.file = abs;
            diagnostics.push_back(d);
            return out;
        }
        text = QString::fromUtf8(file.readAll());
    }

    Lexer lexer;
    const auto lexed = lexer.lex(abs, text);
    diagnostics.append(lexed.diagnostics);
    if (!lexed.diagnostics.isEmpty())
        return out;

    for (const Token& t : lexed.tokens) {
        if (t.kind == TokenKind::Identifier && t.text == token)
            out.push_back(t.span);
    }
    return out;
}

} // namespace

Diagnostic Analyzer::makeDiagnostic(const QString& code, const QString& message, const QString& file)
{
    Diagnostic d;
    d.severity = Severity::Error;
    d.code = code;
    d.message = message;
    d.primary.file = file;
    return d;
}

QString Analyzer::findPackageRoot(const QString& filePath, const QString& workspaceRoot)
{
    QFileInfo cursorInfo(filePath);
    QDir cursor(cursorInfo.isDir() ? cursorInfo.absoluteFilePath() : cursorInfo.absolutePath());
    const QString stop = workspaceRoot.isEmpty() ? QString() : QFileInfo(workspaceRoot).absoluteFilePath();
    while (true) {
        if (isPackageDirectory(cursor.absolutePath()))
            return cursor.absolutePath();
        if (!stop.isEmpty() && cursor.absolutePath() == stop)
            break;
        if (!cursor.cdUp())
            break;
    }
    return {};
}

QString Analyzer::readSourceText(const QString& filePath,
                                 const QHash<QString, QString>& openDocuments,
                                 QList<Diagnostic>& diagnostics)
{
    const QString abs = QFileInfo(filePath).absoluteFilePath();
    if (openDocuments.contains(abs))
        return openDocuments.value(abs);

    QFile file(abs);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        diagnostics.push_back(makeDiagnostic(QStringLiteral("E0004"),
                                             QStringLiteral("cannot open '%1'").arg(abs),
                                             abs));
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QString Analyzer::tokenAtPosition(const QString& filePath,
                                  int zeroBasedLine,
                                  int zeroBasedCharacter,
                                  const QHash<QString, QString>& openDocuments)
{
    QList<Diagnostic> diagnostics;
    const QString text = readSourceText(filePath, openDocuments, diagnostics);
    if (!diagnostics.isEmpty())
        return {};

    const QStringList lines = text.split(QLatin1Char('\n'));
    if (zeroBasedLine < 0 || zeroBasedLine >= lines.size())
        return {};
    const QString line = lines.at(zeroBasedLine);
    int pos = qBound(0, zeroBasedCharacter, line.size());
    if (pos == line.size() && pos > 0)
        --pos;
    auto isIdent = [](QChar ch) {
        return ch.isLetterOrNumber() || ch == QLatin1Char('_');
    };
    if (pos < line.size() && !isIdent(line.at(pos)) && pos > 0 && isIdent(line.at(pos - 1)))
        --pos;
    if (pos < 0 || pos >= line.size() || !isIdent(line.at(pos)))
        return {};

    int start = pos;
    while (start > 0 && isIdent(line.at(start - 1)))
        --start;
    int end = pos + 1;
    while (end < line.size() && isIdent(line.at(end)))
        ++end;
    return line.mid(start, end - start);
}

Analyzer::ParsedProgram Analyzer::parseSources(const QList<PackageSourceFile>& sourceFiles,
                                               const QHash<QString, QString>& openDocuments) const
{
    ParsedProgram result;
    result.program = std::make_unique<ProgramNode>();
    QSet<QString> seen;
    std::vector<PendingSource> pendingSources;

    for (const PackageSourceFile& sourceEntry : sourceFiles) {
        const QString sourceFile = QFileInfo(sourceEntry.path).absoluteFilePath();
        if (seen.contains(sourceFile))
            continue;
        seen.insert(sourceFile);
        result.analyzedFiles.insert(sourceFile);

        QList<Diagnostic> readDiagnostics;
        const QString sourceText = readSourceText(sourceFile, openDocuments, readDiagnostics);
        result.diagnostics.append(readDiagnostics);
        if (!readDiagnostics.isEmpty())
            continue;

        Lexer lexer;
        auto lexed = lexer.lex(sourceFile, sourceText);
        result.diagnostics.append(lexed.diagnostics);
        if (!lexed.diagnostics.isEmpty())
            continue;

        Parser parser;
        auto parsed = parser.parse(lexed.tokens);
        result.diagnostics.append(parsed.diagnostics);
        if (!parsed.diagnostics.isEmpty())
            continue;

        QString moduleName;
        QList<QString> importedModules;
        QList<QString> exportedModules;
        QHash<QString, QString> importedModuleAliases;
        bool importAliasesOk = true;
        for (const auto& decl : parsed.program->declarations) {
            if (auto* module = dynamic_cast<ModuleDeclNode*>(decl.get()))
                moduleName = module->name;
            if (auto* use = dynamic_cast<UseDeclNode*>(decl.get())) {
                importedModules.push_back(use->name);
                if (use->exported)
                    exportedModules.push_back(use->name);
                if (!use->alias.isEmpty()) {
                    if (importedModuleAliases.contains(use->alias)) {
                        Diagnostic d;
                        d.severity = Severity::Error;
                        d.code = QStringLiteral("E0208");
                        d.message = QStringLiteral("duplicate import alias '%1'").arg(use->alias);
                        d.primary = use->span;
                        result.diagnostics.push_back(d);
                        importAliasesOk = false;
                    } else {
                        importedModuleAliases.insert(use->alias, use->name);
                    }
                }
            }
        }
        if (!importAliasesOk)
            continue;

        PendingSource pending;
        pending.sourceEntry = sourceEntry;
        pending.program = std::move(parsed.program);
        pending.moduleName = moduleName;
        pending.importedModules = importedModules;
        pending.exportedModules = exportedModules;
        pending.importedModuleAliases = importedModuleAliases;
        pendingSources.push_back(std::move(pending));
    }

    QHash<QString, QList<QString>> reexportedModules;
    for (const PendingSource& pending : pendingSources) {
        if (pending.moduleName.isEmpty())
            continue;
        QList<QString>& targets = reexportedModules[pending.moduleName];
        for (const QString& module : pending.exportedModules) {
            if (!targets.contains(module))
                targets.push_back(module);
        }
    }

    auto expandImports = [&](const QList<QString>& directImports) {
        QList<QString> expanded;
        QSet<QString> seenImports;
        auto addOne = [&](const QString& module, auto&& addRef) -> void {
            if (module.isEmpty() || seenImports.contains(module))
                return;
            seenImports.insert(module);
            expanded.push_back(module);
            for (const QString& reexport : reexportedModules.value(module))
                addRef(reexport, addRef);
        };
        for (const QString& module : directImports)
            addOne(module, addOne);
        return expanded;
    };

    for (PendingSource& pending : pendingSources) {
        const QList<QString> expandedImports = expandImports(pending.importedModules);
        for (auto& decl : pending.program->declarations) {
            tagDeclarationPackage(*decl, pending.sourceEntry);
            tagDeclarationModule(*decl, pending.moduleName, expandedImports);
            tagDeclarationImportAliases(*decl, pending.importedModuleAliases);
        }
        appendProgram(*result.program, std::move(pending.program));
    }

    for (const QString& file : result.analyzedFiles)
        result.documentSymbols.insert(file, symbolsForDecls(*result.program, file));
    result.symbols = semanticSymbolsForDecls(*result.program);

    return result;
}

AnalyzerResult Analyzer::analyzeWorkspace(const QString& workspaceRoot,
                                          const QHash<QString, QString>& openDocuments) const
{
    AnalyzerResult result;
    const QString root = QFileInfo(workspaceRoot).absoluteFilePath();

    QList<PackageSourceFile> sourceFiles;
    if (!root.isEmpty() && isPackageDirectory(root)) {
        const PackageGraphResult graph = packageGraphFromDirectory(root);
        result.diagnostics.append(graph.diagnostics);
        if (graph.ok()) {
            sourceFiles = packageGraphSourceFileEntries(graph);
        } else {
            return result;
        }
    } else if (!openDocuments.isEmpty()) {
        PackageSourceFile source;
        source.path = openDocuments.constBegin().key();
        source.entry = true;
        sourceFiles.push_back(source);
    } else {
        return result;
    }

    ParsedProgram parsed = parseSources(sourceFiles, openDocuments);
    result.diagnostics.append(parsed.diagnostics);
    result.analyzedFiles = parsed.analyzedFiles;
    result.documentSymbols = parsed.documentSymbols;
    result.symbols = parsed.symbols;
    if (!result.diagnostics.isEmpty())
        return result;

    TypeChecker checker;
    TypeCheckOptions options;
    options.collectAnalysis = true;
    TypeCheckResult checked = checker.check(*parsed.program, options);
    result.diagnostics.append(checked.diagnostics);
    result.analysis = checked.analysis;
    return result;
}

AnalyzerResult Analyzer::analyzeFile(const QString& filePath,
                                     const QHash<QString, QString>& openDocuments,
                                     const QString& workspaceRoot) const
{
    const QString abs = QFileInfo(filePath).absoluteFilePath();
    const QString packageRoot = findPackageRoot(abs, workspaceRoot);

    if (!packageRoot.isEmpty())
        return analyzeWorkspace(packageRoot, openDocuments);

    AnalyzerResult result;
    QList<PackageSourceFile> sourceFiles;
    {
        PackageSourceFile source;
        source.path = abs;
        source.entry = true;
        sourceFiles.push_back(source);
    }

    ParsedProgram parsed = parseSources(sourceFiles, openDocuments);
    result.diagnostics.append(parsed.diagnostics);
    result.analyzedFiles = parsed.analyzedFiles;
    result.documentSymbols = parsed.documentSymbols;
    result.symbols = parsed.symbols;
    if (!result.diagnostics.isEmpty())
        return result;

    TypeChecker checker;
    TypeCheckOptions options;
    options.collectAnalysis = true;
    TypeCheckResult checked = checker.check(*parsed.program, options);
    result.diagnostics.append(checked.diagnostics);
    result.analysis = checked.analysis;
    return result;
}

QJsonObject Analyzer::hover(const QString& filePath,
                            int zeroBasedLine,
                            int zeroBasedCharacter,
                            const QHash<QString, QString>& openDocuments,
                            const QString& workspaceRoot) const
{
    const AnalyzerResult analyzed = analyzeFile(filePath, openDocuments, workspaceRoot);
    const QString abs = QFileInfo(filePath).absoluteFilePath();
    const int oneBasedLine = zeroBasedLine + 1;
    const int oneBasedColumn = zeroBasedCharacter + 1;
    const QString token = tokenAtPosition(abs, zeroBasedLine, zeroBasedCharacter, openDocuments);
    if (analyzed.analysis) {
        const AnalysisBinding* binding = analyzed.analysis->bindingAt(abs, oneBasedLine, oneBasedColumn);
        const AnalysisSymbol* symbol = binding ? analyzed.analysis->symbolById(binding->symbol) : nullptr;
        const AnalysisExprInfo* expr = analyzed.analysis->exprInfoAt(abs, oneBasedLine, oneBasedColumn);
        if (symbol || (expr && token.isEmpty())) {
            QString value;
            if (symbol) {
                value += QStringLiteral("```abel\n%1\n```").arg(analysisSymbolHoverLine(*symbol));
                if (!symbol->container.isEmpty())
                    value += QStringLiteral("\n\ncontainer: `%1`").arg(symbol->container);
            }
            if (expr) {
                if (!value.isEmpty())
                    value += QStringLiteral("\n\n");
                value += QStringLiteral("type: `%1`  \ncategory: `%2`  \nmutable: `%3`")
                             .arg(expr->type.displayName(),
                                  valueCategoryName(expr->category),
                                  expr->isMutable ? QStringLiteral("yes") : QStringLiteral("no"));
            }

            QJsonObject contents;
            contents.insert(QStringLiteral("kind"), QStringLiteral("markdown"));
            contents.insert(QStringLiteral("value"), value);
            QJsonObject hover;
            hover.insert(QStringLiteral("contents"), contents);
            if (binding)
                hover.insert(QStringLiteral("range"), rangeFromSpan(binding->use));
            else if (expr)
                hover.insert(QStringLiteral("range"), rangeFromSpan(expr->span));
            return hover;
        }
    }

    const IndexedSymbol* best = bestSymbolForToken(analyzed.symbols, token, abs, zeroBasedLine, zeroBasedCharacter);
    if (!best) {
        for (const IndexedSymbol& symbol : analyzed.symbols) {
            if (containsPosition(symbol.selectionRange, abs, zeroBasedLine, zeroBasedCharacter)) {
                if (!best
                    || (symbol.selectionRange.endLine - symbol.selectionRange.startLine)
                        <= (best->selectionRange.endLine - best->selectionRange.startLine)) {
                    best = &symbol;
                }
            }
        }
    }
    if (!best)
        return {};

    QString value = QStringLiteral("```abel\n%1\n```").arg(best->detail);
    if (!best->containerName.isEmpty())
        value += QStringLiteral("\n\ncontainer: `%1`").arg(best->containerName);

    QJsonObject contents;
    contents.insert(QStringLiteral("kind"), QStringLiteral("markdown"));
    contents.insert(QStringLiteral("value"), value);

    QJsonObject hover;
    hover.insert(QStringLiteral("contents"), contents);
    hover.insert(QStringLiteral("range"), rangeFromSpan(best->selectionRange));
    return hover;
}

QJsonArray Analyzer::definitions(const QString& filePath,
                                 int zeroBasedLine,
                                 int zeroBasedCharacter,
                                 const QHash<QString, QString>& openDocuments,
                                 const QString& workspaceRoot) const
{
    QJsonArray out;
    const AnalyzerResult analyzed = analyzeFile(filePath, openDocuments, workspaceRoot);
    const QString abs = QFileInfo(filePath).absoluteFilePath();
    if (analyzed.analysis) {
        const AnalysisBinding* binding = analyzed.analysis->bindingAt(abs, zeroBasedLine + 1, zeroBasedCharacter + 1);
        const AnalysisSymbol* symbol = binding ? analyzed.analysis->symbolById(binding->symbol) : nullptr;
        if (symbol) {
            out.push_back(locationFromAnalysisSymbol(*symbol));
            return out;
        }
    }

    QString token = tokenAtPosition(abs, zeroBasedLine, zeroBasedCharacter, openDocuments);

    if (token.isEmpty()) {
        for (const IndexedSymbol& symbol : analyzed.symbols) {
            if (containsPosition(symbol.selectionRange, abs, zeroBasedLine, zeroBasedCharacter)) {
                token = symbol.name;
                break;
            }
        }
    }
    if (token.isEmpty())
        return out;

    const IndexedSymbol* selected = bestSymbolForToken(analyzed.symbols, token, abs, zeroBasedLine, zeroBasedCharacter);
    if (selected && selected->local) {
        out.push_back(locationFromSymbol(*selected));
        return out;
    }

    for (const IndexedSymbol& symbol : analyzed.symbols) {
        if (symbol.name == token && (!selected || symbol.local == selected->local))
            out.push_back(locationFromSymbol(symbol));
    }
    return out;
}

QJsonArray Analyzer::workspaceSymbols(const QString& query,
                                      const QString& workspaceRoot,
                                      const QHash<QString, QString>& openDocuments) const
{
    QJsonArray out;
    const AnalyzerResult analyzed = analyzeWorkspace(workspaceRoot, openDocuments);
    const QString needle = query.trimmed();
    for (const IndexedSymbol& symbol : analyzed.symbols) {
        if (needle.isEmpty() || symbol.name.contains(needle, Qt::CaseInsensitive)
            || symbol.detail.contains(needle, Qt::CaseInsensitive)) {
            out.push_back(workspaceSymbolFromIndexed(symbol));
        }
    }
    return out;
}

QJsonArray Analyzer::references(const QString& filePath,
                                int zeroBasedLine,
                                int zeroBasedCharacter,
                                const QHash<QString, QString>& openDocuments,
                                const QString& workspaceRoot) const
{
    QJsonArray out;
    const QString abs = QFileInfo(filePath).absoluteFilePath();
    const AnalyzerResult analyzed = analyzeFile(abs, openDocuments, workspaceRoot);
    if (analyzed.analysis) {
        const AnalysisBinding* binding = analyzed.analysis->bindingAt(abs, zeroBasedLine + 1, zeroBasedCharacter + 1);
        if (binding) {
            const AnalysisSymbol* symbol = analyzed.analysis->symbolById(binding->symbol);
            if (symbol)
                out.push_back(locationFromAnalysisSymbol(*symbol));
            for (const AnalysisBinding& use : analyzed.analysis->bindingsForSymbol(binding->symbol))
                out.push_back(locationFromAnalysisBinding(use));
            return out;
        }
    }

    const QString token = tokenAtPosition(abs, zeroBasedLine, zeroBasedCharacter, openDocuments);
    const IndexedSymbol* selected = bestSymbolForToken(analyzed.symbols, token, abs, zeroBasedLine, zeroBasedCharacter);
    if (!selected)
        return out;

    for (const QString& file : analyzed.analyzedFiles) {
        QList<Diagnostic> diagnostics;
        const QList<SourceSpan> spans = tokenSpansInFile(file, token, openDocuments, diagnostics);
        if (!diagnostics.isEmpty())
            continue;
        for (const SourceSpan& span : spans) {
            if (!tokenBelongsToSelectedSymbol(selected, token, span))
                continue;
            QJsonObject location;
            location.insert(QStringLiteral("uri"), uriFromPath(span.file));
            location.insert(QStringLiteral("range"), rangeFromSpan(span));
            out.push_back(location);
        }
    }
    return out;
}

QJsonArray Analyzer::documentHighlights(const QString& filePath,
                                        int zeroBasedLine,
                                        int zeroBasedCharacter,
                                        const QHash<QString, QString>& openDocuments,
                                        const QString& workspaceRoot) const
{
    QJsonArray out;
    const QString abs = QFileInfo(filePath).absoluteFilePath();
    const QJsonArray refs = references(abs, zeroBasedLine, zeroBasedCharacter, openDocuments, workspaceRoot);
    for (const QJsonValue& value : refs) {
        const QJsonObject location = value.toObject();
        if (pathFromUri(location.value(QStringLiteral("uri")).toString()) != abs)
            continue;
        QJsonObject highlight;
        highlight.insert(QStringLiteral("range"), location.value(QStringLiteral("range")).toObject());
        highlight.insert(QStringLiteral("kind"), 1);
        out.push_back(highlight);
    }
    return out;
}

QJsonArray Analyzer::completionItems(const QString& filePath,
                                     const QHash<QString, QString>& openDocuments,
                                     const QString& workspaceRoot) const
{
    QJsonArray out;
    const AnalyzerResult analyzed = analyzeFile(filePath, openDocuments, workspaceRoot);
    QSet<QString> seen;
    for (const IndexedSymbol& symbol : analyzed.symbols) {
        if (symbol.name.isEmpty() || seen.contains(symbol.name + QStringLiteral("\n") + symbol.detail))
            continue;
        seen.insert(symbol.name + QStringLiteral("\n") + symbol.detail);
        out.push_back(completionItemFromSymbol(symbol));
    }
    return out;
}

QJsonArray Analyzer::completionItems(const QString& filePath,
                                     int zeroBasedLine,
                                     int zeroBasedCharacter,
                                     const QHash<QString, QString>& openDocuments,
                                     const QString& workspaceRoot) const
{
    QList<Diagnostic> readDiagnostics;
    const QString text = readSourceText(filePath, openDocuments, readDiagnostics);
    if (!readDiagnostics.isEmpty())
        return completionItems(filePath, openDocuments, workspaceRoot);

    const MemberCompletionContext member = memberCompletionContext(text, zeroBasedLine, zeroBasedCharacter);
    if (!member.active)
        return completionItems(filePath, openDocuments, workspaceRoot);

    QHash<QString, QString> patchedDocuments = openDocuments;
    if (member.needsPatch) {
        const QString abs = QFileInfo(filePath).absoluteFilePath();
        patchedDocuments.insert(abs, member.patchedText);
        patchedDocuments.insert(filePath, member.patchedText);
    }

    const AnalyzerResult analyzed = analyzeFile(filePath, patchedDocuments, workspaceRoot);
    const QString abs = QFileInfo(filePath).absoluteFilePath();
    const AnalysisExprInfo* receiver = analyzed.analysis
        ? analyzed.analysis->exprInfoAt(abs, member.receiverLine + 1, member.receiverColumn + 1)
        : nullptr;
    if (!receiver || receiver->type.kind != TypeKind::Struct)
        return completionItems(filePath, openDocuments, workspaceRoot);

    const QString structName = receiver->type.spelling.isEmpty()
        ? receiver->type.displayName()
        : receiver->type.spelling;

    QJsonArray out;
    QSet<QString> seen;
    for (const IndexedSymbol& symbol : analyzed.symbols) {
        if (symbol.containerName != structName)
            continue;
        if (symbol.kind != 6 && symbol.kind != 8)
            continue;
        const QString key = symbol.name + QStringLiteral("\n") + symbol.detail;
        if (seen.contains(key))
            continue;
        seen.insert(key);
        out.push_back(completionItemFromSymbol(symbol));
    }
    if (!out.isEmpty())
        return out;
    return completionItems(filePath, openDocuments, workspaceRoot);
}

QJsonObject Analyzer::signatureHelp(const QString& filePath,
                                    int zeroBasedLine,
                                    int zeroBasedCharacter,
                                    const QHash<QString, QString>& openDocuments,
                                    const QString& workspaceRoot) const
{
    QList<Diagnostic> readDiagnostics;
    const QString text = readSourceText(filePath, openDocuments, readDiagnostics);
    if (!readDiagnostics.isEmpty())
        return {};

    const QStringList lines = text.split(QLatin1Char('\n'));
    if (zeroBasedLine < 0 || zeroBasedLine >= lines.size())
        return {};
    const QString line = lines.at(zeroBasedLine);
    int pos = qBound(0, zeroBasedCharacter, line.size());

    int depth = 0;
    int openParen = -1;
    for (int i = pos - 1; i >= 0; --i) {
        const QChar ch = line.at(i);
        if (ch == QLatin1Char(')')) {
            ++depth;
        } else if (ch == QLatin1Char('(')) {
            if (depth == 0) {
                openParen = i;
                break;
            }
            --depth;
        }
    }
    if (openParen <= 0)
        return {};

    auto isIdent = [](QChar ch) {
        return ch.isLetterOrNumber() || ch == QLatin1Char('_');
    };
    int nameEnd = openParen;
    int nameStart = nameEnd;
    while (nameStart > 0 && line.at(nameStart - 1).isSpace())
        --nameStart;
    nameEnd = nameStart;
    while (nameStart > 0 && isIdent(line.at(nameStart - 1)))
        --nameStart;
    const QString name = line.mid(nameStart, nameEnd - nameStart);
    if (name.isEmpty())
        return {};

    int activeParameter = 0;
    int nested = 0;
    for (int i = openParen + 1; i < pos && i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (ch == QLatin1Char('(') || ch == QLatin1Char('[') || ch == QLatin1Char('{'))
            ++nested;
        else if ((ch == QLatin1Char(')') || ch == QLatin1Char(']') || ch == QLatin1Char('}')) && nested > 0)
            --nested;
        else if (ch == QLatin1Char(',') && nested == 0)
            ++activeParameter;
    }

    const AnalyzerResult analyzed = analyzeFile(filePath, openDocuments, workspaceRoot);
    QJsonArray signatures;
    for (const IndexedSymbol& symbol : analyzed.symbols) {
        if (symbol.name != name || symbol.local)
            continue;
        if (!(symbol.detail.contains(QStringLiteral("fn "))
              || symbol.detail.startsWith(QStringLiteral("init("))
              || symbol.detail.contains(QStringLiteral("::fn "))))
            continue;
        QJsonObject signature;
        signature.insert(QStringLiteral("label"), symbol.detail);
        QJsonObject docs;
        docs.insert(QStringLiteral("kind"), QStringLiteral("markdown"));
        docs.insert(QStringLiteral("value"), QStringLiteral("```abel\n%1\n```").arg(symbol.detail));
        signature.insert(QStringLiteral("documentation"), docs);
        signatures.push_back(signature);
    }
    if (signatures.isEmpty())
        return {};

    QJsonObject help;
    help.insert(QStringLiteral("signatures"), signatures);
    help.insert(QStringLiteral("activeSignature"), 0);
    help.insert(QStringLiteral("activeParameter"), activeParameter);
    return help;
}

QJsonArray Analyzer::foldingRanges(const QString& filePath,
                                   const QHash<QString, QString>& openDocuments,
                                   const QString& workspaceRoot) const
{
    QJsonArray out;
    const QString abs = QFileInfo(filePath).absoluteFilePath();
    const AnalyzerResult analyzed = analyzeFile(abs, openDocuments, workspaceRoot);
    QSet<QString> seen;
    for (const IndexedSymbol& symbol : analyzed.symbols) {
        if (symbol.local)
            continue;
        if (symbol.file != abs)
            continue;
        if (symbol.range.endLine <= symbol.range.startLine)
            continue;
        const QString key = QStringLiteral("%1:%2:%3").arg(symbol.range.startLine).arg(symbol.range.endLine).arg(symbol.name);
        if (seen.contains(key))
            continue;
        seen.insert(key);

        QJsonObject range;
        range.insert(QStringLiteral("startLine"), qMax(0, symbol.range.startLine - 1));
        range.insert(QStringLiteral("endLine"), qMax(0, symbol.range.endLine - 1));
        range.insert(QStringLiteral("startCharacter"), qMax(0, symbol.range.startColumn - 1));
        range.insert(QStringLiteral("endCharacter"), qMax(0, symbol.range.endColumn - 1));
        range.insert(QStringLiteral("kind"), QStringLiteral("region"));
        out.push_back(range);
    }
    return out;
}

QJsonObject Analyzer::semanticTokens(const QString& filePath,
                                     const QHash<QString, QString>& openDocuments,
                                     const QString& workspaceRoot) const
{
    QJsonObject result;
    QJsonArray data;
    QList<Diagnostic> diagnostics;
    const QString abs = QFileInfo(filePath).absoluteFilePath();
    const QString text = readSourceText(abs, openDocuments, diagnostics);
    if (!diagnostics.isEmpty()) {
        result.insert(QStringLiteral("data"), data);
        return result;
    }

    const AnalyzerResult analyzed = analyzeFile(abs, openDocuments, workspaceRoot);
    Lexer lexer;
    const auto lexed = lexer.lex(abs, text);
    if (!lexed.diagnostics.isEmpty()) {
        result.insert(QStringLiteral("data"), data);
        return result;
    }

    int lastLine = 0;
    int lastStart = 0;
    bool first = true;
    for (const Token& token : lexed.tokens) {
        if (token.kind == TokenKind::End)
            continue;
        const int tokenType = semanticKindForToken(token, analyzed.symbols);
        if (tokenType < 0)
            continue;

        const int line = qMax(0, token.span.startLine - 1);
        const int start = qMax(0, token.span.startColumn - 1);
        const int length = qMax(1, token.span.endColumn - token.span.startColumn);
        const int deltaLine = first ? line : line - lastLine;
        const int deltaStart = first || deltaLine != 0 ? start : start - lastStart;
        data.push_back(deltaLine);
        data.push_back(deltaStart);
        data.push_back(length);
        data.push_back(tokenType);
        data.push_back(0);
        lastLine = line;
        lastStart = start;
        first = false;
    }
    result.insert(QStringLiteral("data"), data);
    return result;
}

} // namespace abel::lsp
