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
                          const QString& container = {})
{
    IndexedSymbol symbol;
    symbol.name = name;
    symbol.detail = detail;
    symbol.containerName = container;
    symbol.file = QFileInfo(span.file).absoluteFilePath();
    symbol.kind = kind;
    symbol.range = span;
    symbol.selectionRange = span;
    return symbol;
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
            for (const auto& ctor : s->constructors)
                symbols.push_back(makeIndexed(QStringLiteral("init"), constructorDisplay(*ctor), 9, ctor->span, s->name));
            for (const auto& method : s->methods)
                symbols.push_back(makeIndexed(method->name, functionDisplay(*method), 6, method->span, s->name));
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
    TypeCheckResult checked = checker.check(*parsed.program);
    result.diagnostics.append(checked.diagnostics);
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
    TypeCheckResult checked = checker.check(*parsed.program);
    result.diagnostics.append(checked.diagnostics);
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
    const QString token = tokenAtPosition(abs, zeroBasedLine, zeroBasedCharacter, openDocuments);

    const IndexedSymbol* best = nullptr;
    if (!token.isEmpty()) {
        for (const IndexedSymbol& symbol : analyzed.symbols) {
            if (symbol.name == token) {
                best = &symbol;
                break;
            }
        }
    }
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

    for (const IndexedSymbol& symbol : analyzed.symbols) {
        if (symbol.name == token)
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

} // namespace abel::lsp
