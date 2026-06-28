#pragma once

#include "abelcore/diagnostic.h"
#include "abelcore/package_manifest.h"
#include "abelcore/source_span.h"
#include "abelcore/analysis_index.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>

#include <memory>

namespace abel {

struct DeclNode;
struct ProgramNode;

namespace lsp {

struct AnalyzerDocument {
    QString path;
    QString text;
};

struct IndexedSymbol {
    QString name;
    QString detail;
    QString containerName;
    QString file;
    int kind = 13;
    bool local = false;
    SourceSpan range;
    SourceSpan selectionRange;
    SourceSpan scopeRange;
};

struct AnalyzerResult {
    QList<Diagnostic> diagnostics;
    QSet<QString> analyzedFiles;
    QHash<QString, QJsonArray> documentSymbols;
    QList<IndexedSymbol> symbols;
    std::shared_ptr<AnalysisIndex> analysis;
};

class Analyzer {
public:
    AnalyzerResult analyzeFile(const QString& filePath,
                               const QHash<QString, QString>& openDocuments,
                               const QString& workspaceRoot = {}) const;
    AnalyzerResult analyzeWorkspace(const QString& workspaceRoot,
                                    const QHash<QString, QString>& openDocuments) const;

    QJsonObject hover(const QString& filePath,
                      int zeroBasedLine,
                      int zeroBasedCharacter,
                      const QHash<QString, QString>& openDocuments,
                      const QString& workspaceRoot = {}) const;
    QJsonArray definitions(const QString& filePath,
                           int zeroBasedLine,
                           int zeroBasedCharacter,
                           const QHash<QString, QString>& openDocuments,
                           const QString& workspaceRoot = {}) const;
    QJsonArray workspaceSymbols(const QString& query,
                                const QString& workspaceRoot,
                                const QHash<QString, QString>& openDocuments) const;
    QJsonArray references(const QString& filePath,
                          int zeroBasedLine,
                          int zeroBasedCharacter,
                          const QHash<QString, QString>& openDocuments,
                          const QString& workspaceRoot = {}) const;
    QJsonArray documentHighlights(const QString& filePath,
                                  int zeroBasedLine,
                                  int zeroBasedCharacter,
                                  const QHash<QString, QString>& openDocuments,
                                  const QString& workspaceRoot = {}) const;
    QJsonArray completionItems(const QString& filePath,
                               const QHash<QString, QString>& openDocuments,
                               const QString& workspaceRoot = {}) const;
    QJsonArray completionItems(const QString& filePath,
                               int zeroBasedLine,
                               int zeroBasedCharacter,
                               const QHash<QString, QString>& openDocuments,
                               const QString& workspaceRoot = {}) const;
    QJsonObject signatureHelp(const QString& filePath,
                              int zeroBasedLine,
                              int zeroBasedCharacter,
                              const QHash<QString, QString>& openDocuments,
                              const QString& workspaceRoot = {}) const;
    QJsonArray foldingRanges(const QString& filePath,
                             const QHash<QString, QString>& openDocuments,
                             const QString& workspaceRoot = {}) const;
    QJsonObject semanticTokens(const QString& filePath,
                               const QHash<QString, QString>& openDocuments,
                               const QString& workspaceRoot = {}) const;

private:
    struct ParsedProgram {
        std::unique_ptr<ProgramNode> program;
        QList<Diagnostic> diagnostics;
        QSet<QString> analyzedFiles;
        QHash<QString, QJsonArray> documentSymbols;
        QList<IndexedSymbol> symbols;
    };

    static Diagnostic makeDiagnostic(const QString& code, const QString& message, const QString& file = {});
    static QString findPackageRoot(const QString& filePath, const QString& workspaceRoot);
    static QString readSourceText(const QString& filePath, const QHash<QString, QString>& openDocuments, QList<Diagnostic>& diagnostics);
    static QString tokenAtPosition(const QString& filePath,
                                   int zeroBasedLine,
                                   int zeroBasedCharacter,
                                   const QHash<QString, QString>& openDocuments);

    ParsedProgram parseSources(const QList<PackageSourceFile>& sourceFiles,
                               const QHash<QString, QString>& openDocuments) const;
};

} // namespace lsp
} // namespace abel
