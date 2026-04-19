#include "shared/test_fixture_loader.h"

#include "infrastructure/data/conclusion_index_repository.h"

#include <QDir>
#include <QStringList>

#include <algorithm>

#ifndef MATH_SEARCH_PROJECT_SOURCE_DIR
#error "MATH_SEARCH_PROJECT_SOURCE_DIR is not defined for tests"
#endif

#ifndef MATH_SEARCH_TESTS_SOURCE_DIR
#error "MATH_SEARCH_TESTS_SOURCE_DIR is not defined for tests"
#endif

namespace tests::shared {

QString projectSourceDir()
{
    return QDir(QString::fromUtf8(MATH_SEARCH_PROJECT_SOURCE_DIR)).absolutePath();
}

QString testsSourceDir()
{
    return QDir(QString::fromUtf8(MATH_SEARCH_TESTS_SOURCE_DIR)).absolutePath();
}

QString fixtureIndexPath()
{
    return QDir(testsSourceDir()).filePath(QStringLiteral("fixtures/test_backend_search_index.json"));
}

QString realIndexPath()
{
    return QDir(projectSourceDir()).filePath(QStringLiteral("data/backend_search_index.json"));
}

QString summarizeDiagnostics(const infrastructure::data::ConclusionIndexRepository& repository)
{
    const auto& diagnostics = repository.diagnostics();

    QStringList rows;
    rows.push_back(QStringLiteral("success=%1").arg(diagnostics.isSuccess() ? QStringLiteral("true") : QStringLiteral("false")));
    rows.push_back(QStringLiteral("fatalError=%1").arg(diagnostics.fatalError));
    rows.push_back(QStringLiteral("loadedDocs=%1 skippedDocs=%2 loadedTerms=%3 loadedPrefixes=%4 skippedPostings=%5")
                       .arg(diagnostics.loadedDocCount)
                       .arg(diagnostics.skippedDocCount)
                       .arg(diagnostics.loadedTermCount)
                       .arg(diagnostics.loadedPrefixCount)
                       .arg(diagnostics.skippedPostingCount));
    if (!diagnostics.warnings.isEmpty()) {
        const int warningPreviewCount = std::min(3, static_cast<int>(diagnostics.warnings.size()));
        QStringList warningPreview;
        warningPreview.reserve(warningPreviewCount);
        for (int i = 0; i < warningPreviewCount; ++i) {
            warningPreview.push_back(diagnostics.warnings.at(i));
        }
        rows.push_back(QStringLiteral("warnings=%1 firstWarnings=%2")
                           .arg(diagnostics.warnings.size())
                           .arg(warningPreview.join(QStringLiteral(" || "))));
    } else {
        rows.push_back(QStringLiteral("warnings=0"));
    }

    return rows.join(QStringLiteral("; "));
}

bool loadRepositoryFromFile(const QString& filePath,
                            infrastructure::data::ConclusionIndexRepository* repository,
                            QString* errorSummary)
{
    if (repository == nullptr) {
        if (errorSummary != nullptr) {
            *errorSummary = QStringLiteral("repository pointer is null");
        }
        return false;
    }

    if (!repository->loadFromFile(filePath)) {
        if (errorSummary != nullptr) {
            *errorSummary = QStringLiteral("failed to load index file path=%1; %2")
                                .arg(filePath, summarizeDiagnostics(*repository));
        }
        return false;
    }
    return true;
}

}  // namespace tests::shared
