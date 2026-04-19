#include <QApplication>
#include <QCoreApplication>

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"
#include "domain/adapters/conclusion_card_adapter.h"
#include "domain/adapters/conclusion_detail_adapter.h"
#include "domain/services/search_service.h"
#include "domain/services/suggest_service.h"
#include "infrastructure/data/conclusion_index_repository.h"
#include "infrastructure/data/conclusion_content_repository.h"
#include "ui/main_window.h"

namespace {

bool hasFlag(const QString& flag)
{
    return QCoreApplication::arguments().contains(flag, Qt::CaseInsensitive);
}

bool contentProbeEnabled()
{
    const QString raw = qEnvironmentVariable("MATH_SEARCH_CONTENT_PROBE").trimmed().toLower();
    return raw == QStringLiteral("1") || raw == QStringLiteral("true") || raw == QStringLiteral("yes")
           || raw == QStringLiteral("on") || hasFlag(QStringLiteral("--content-probe"));
}

bool indexProbeEnabled()
{
    const QString raw = qEnvironmentVariable("MATH_SEARCH_INDEX_PROBE").trimmed().toLower();
    return raw == QStringLiteral("1") || raw == QStringLiteral("true") || raw == QStringLiteral("yes")
           || raw == QStringLiteral("on") || hasFlag(QStringLiteral("--index-probe"));
}

bool probeOnlyEnabled()
{
    return hasFlag(QStringLiteral("--probe-only"));
}

void runContentProbeIfEnabled()
{
    if (!contentProbeEnabled()) {
        return;
    }

    infrastructure::data::ConclusionContentRepository repository;
    if (!repository.loadFromFile()) {
        const auto& diagnostics = repository.diagnostics();
        LOG_ERROR(LogCategory::DataLoader,
                  QStringLiteral("content probe failed fatal=%1 skipped=%2 warnings=%3")
                      .arg(diagnostics.fatalError)
                      .arg(diagnostics.skippedCount)
                      .arg(diagnostics.warnings.size()));
        return;
    }

    const auto& diagnostics = repository.diagnostics();
    LOG_INFO(LogCategory::DataLoader,
             QStringLiteral("content probe loaded_count=%1 skipped_count=%2 modules=%3 tags=%4 path=%5")
                 .arg(diagnostics.loadedCount)
                 .arg(diagnostics.skippedCount)
                 .arg(repository.modules().size())
                 .arg(repository.tags().size())
                 .arg(repository.activeContentPath()));

    QString firstId;
    repository.forEachRecord([&firstId](const QString& id, const domain::models::ConclusionRecord&) {
        if (firstId.isEmpty()) {
            firstId = id;
        }
    });
    if (firstId.isEmpty()) {
        LOG_WARN(LogCategory::DataLoader, QStringLiteral("content probe found no records"));
        return;
    }

    const auto* record = repository.getById(firstId);
    if (record == nullptr) {
        LOG_WARN(LogCategory::DataLoader, QStringLiteral("content probe failed to get first id=%1").arg(firstId));
        return;
    }

    const domain::adapters::ConclusionCardViewData cardView = domain::adapters::ConclusionCardAdapter::toViewData(*record);
    const domain::adapters::ConclusionDetailViewData detailView =
        domain::adapters::ConclusionDetailAdapter::toViewData(*record);

    LOG_INFO(LogCategory::DataLoader,
             QStringLiteral("content probe sample id=%1 cardTitle=%2 detailSections=%3")
                 .arg(cardView.id, cardView.title)
                 .arg(detailView.sections.size()));
}

void runIndexProbeIfEnabled()
{
    if (!indexProbeEnabled()) {
        return;
    }

    infrastructure::data::ConclusionIndexRepository repository;
    if (!repository.loadFromFile()) {
        const auto& diagnostics = repository.diagnostics();
        LOG_ERROR(LogCategory::SearchIndex,
                  QStringLiteral("index probe failed fatal=%1 warnings=%2 loadedDocs=%3 skippedDocs=%4 skippedPostings=%5")
                      .arg(diagnostics.fatalError)
                      .arg(diagnostics.warnings.size())
                      .arg(diagnostics.loadedDocCount)
                      .arg(diagnostics.skippedDocCount)
                      .arg(diagnostics.skippedPostingCount));
        return;
    }

    LOG_INFO(LogCategory::SearchIndex,
             QStringLiteral("index probe loaded docs=%1 terms=%2 prefixes=%3 suggestions=%4 modules=%5 path=%6")
                 .arg(repository.docCount())
                 .arg(repository.termCount())
                 .arg(repository.prefixCount())
                 .arg(repository.suggestionCount())
                 .arg(repository.modules().size())
                 .arg(repository.activeIndexPath()));

    domain::services::SearchService searchService(&repository);
    domain::services::SuggestService suggestService(&repository);

    const domain::models::SearchResult searchResult = searchService.search(QStringLiteral("不等式"));
    const domain::models::SuggestionResult suggestionResult = suggestService.suggest(QStringLiteral("对数"));
    const QString firstHit = searchResult.hits.isEmpty() ? QStringLiteral("<none>") : searchResult.hits.first().docId;
    const QString firstSuggestion =
        suggestionResult.items.isEmpty() ? QStringLiteral("<none>") : suggestionResult.items.first().text;

    LOG_INFO(LogCategory::SearchEngine,
             QStringLiteral("index probe search query=不等式 total=%1 firstDoc=%2")
                 .arg(searchResult.total)
                 .arg(firstHit));
    LOG_INFO(LogCategory::SearchEngine,
             QStringLiteral("index probe suggest query=对数 total=%1 firstSuggestion=%2")
                 .arg(suggestionResult.total)
                 .arg(firstSuggestion));
}

}  // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("math_search"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("local.offline"));
    QCoreApplication::setApplicationName(QStringLiteral("math_search_win"));

    logging::Logger::instance().initialize();
    LOG_INFO(LogCategory::AppStartup,
             QStringLiteral("application startup app=%1 logDir=%2")
                 .arg(QCoreApplication::applicationName(), logging::Logger::instance().logDirectory()));
    runContentProbeIfEnabled();
    runIndexProbeIfEnabled();
    if (probeOnlyEnabled()) {
        LOG_INFO(LogCategory::AppShutdown, QStringLiteral("probe-only mode complete, exiting before UI startup"));
        logging::Logger::instance().shutdown();
        return 0;
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        LOG_INFO(LogCategory::AppShutdown, QStringLiteral("aboutToQuit signal received"));
    });

    MainWindow window;
    window.show();
    LOG_INFO(LogCategory::AppStartup, QStringLiteral("main window created and shown"));

    const int exitCode = app.exec();
    LOG_INFO(LogCategory::AppShutdown, QStringLiteral("application exited exitCode=%1").arg(exitCode));
    logging::Logger::instance().shutdown();
    return exitCode;
}
