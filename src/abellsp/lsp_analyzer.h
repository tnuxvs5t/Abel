#pragma once

#include "abelcore/diagnostic.h"
#include "abelcore/package_manifest.h"
#include "abelcore/source_span.h"

#include <QHash>
#include <QJsonArray>
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

struct AnalyzerResult {
    QList<Diagnostic> diagnostics;
    QSet<QString> analyzedFiles;
    QHash<QString, QJsonArray> documentSymbols;
};

class Analyzer {
public:
    AnalyzerResult analyzeFile(const QString& filePath,
                               const QHash<QString, QString>& openDocuments,
                               const QString& workspaceRoot = {}) const;

private:
    struct ParsedProgram {
        std::unique_ptr<ProgramNode> program;
        QList<Diagnostic> diagnostics;
        QSet<QString> analyzedFiles;
        QHash<QString, QJsonArray> documentSymbols;
    };

    static Diagnostic makeDiagnostic(const QString& code, const QString& message, const QString& file = {});
    static QString findPackageRoot(const QString& filePath, const QString& workspaceRoot);
    static QString readSourceText(const QString& filePath, const QHash<QString, QString>& openDocuments, QList<Diagnostic>& diagnostics);

    ParsedProgram parseSources(const QList<PackageSourceFile>& sourceFiles,
                               const QHash<QString, QString>& openDocuments) const;
};

} // namespace lsp
} // namespace abel
