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

    return result;
}

AnalyzerResult Analyzer::analyzeFile(const QString& filePath,
                                     const QHash<QString, QString>& openDocuments,
                                     const QString& workspaceRoot) const
{
    AnalyzerResult result;
    const QString abs = QFileInfo(filePath).absoluteFilePath();
    const QString packageRoot = findPackageRoot(abs, workspaceRoot);

    QList<PackageSourceFile> sourceFiles;
    if (!packageRoot.isEmpty()) {
        const PackageGraphResult graph = packageGraphFromDirectory(packageRoot);
        result.diagnostics.append(graph.diagnostics);
        if (graph.ok()) {
            sourceFiles = packageGraphSourceFileEntries(graph);
        } else {
            result.analyzedFiles.insert(abs);
            return result;
        }
    } else {
        PackageSourceFile source;
        source.path = abs;
        source.entry = true;
        sourceFiles.push_back(source);
    }

    ParsedProgram parsed = parseSources(sourceFiles, openDocuments);
    result.diagnostics.append(parsed.diagnostics);
    result.analyzedFiles = parsed.analyzedFiles;
    result.documentSymbols = parsed.documentSymbols;
    if (!result.diagnostics.isEmpty())
        return result;

    TypeChecker checker;
    TypeCheckResult checked = checker.check(*parsed.program);
    result.diagnostics.append(checked.diagnostics);
    return result;
}

} // namespace abel::lsp
