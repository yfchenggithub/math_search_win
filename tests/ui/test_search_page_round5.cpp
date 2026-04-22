#include "core/logging/logger.h"
#include "domain/adapters/conclusion_detail_adapter.h"
#include "domain/repositories/favorites_repository.h"
#include "domain/repositories/history_repository.h"
#include "domain/services/search_service.h"
#include "domain/services/suggest_service.h"
#include "infrastructure/data/conclusion_index_repository.h"
#include "ui/detail/detail_html_renderer.h"
#include "ui/detail/detail_pane.h"
#include "ui/detail/detail_render_coordinator.h"
#include "ui/detail/detail_view_data_mapper.h"

#define private public
#include "ui/pages/search_page.h"
#undef private

#include <QtTest/QtTest>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSignalSpy>

#include <algorithm>
#include <utility>

#ifndef MATH_SEARCH_TESTS_SOURCE_DIR
#error "MATH_SEARCH_TESTS_SOURCE_DIR is not defined for tests"
#endif

namespace {

QString testsSourceDir()
{
    return QDir(QString::fromUtf8(MATH_SEARCH_TESTS_SOURCE_DIR)).absolutePath();
}

bool copyFileStrict(const QString& sourcePath, const QString& targetPath)
{
    if (!QFileInfo::exists(sourcePath)) {
        return false;
    }

    const QFileInfo targetInfo(targetPath);
    QDir targetDir = targetInfo.absoluteDir();
    if (!targetDir.exists() && !QDir().mkpath(targetDir.absolutePath())) {
        return false;
    }

    if (QFileInfo::exists(targetPath) && !QFile::remove(targetPath)) {
        return false;
    }
    return QFile::copy(sourcePath, targetPath);
}

QString readUtf8File(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QByteArray bytes = file.readAll();
    file.close();
    return QString::fromUtf8(bytes);
}

struct DetailDispatchLogEvent {
    enum class Type {
        ResetBeforeDispatch,
        DispatchStart,
    };

    Type type = Type::ResetBeforeDispatch;
    quint64 requestId = 0;
};

QVector<DetailDispatchLogEvent> parseDetailDispatchLogEvents(const QString& logText)
{
    QVector<DetailDispatchLogEvent> events;
    static const QRegularExpression resetPattern(
        QStringLiteral("event=detail_viewport_reset\\s+request_id=(\\d+)\\s+detail_id=([^\\s]+)\\s+stage=before_dispatch"));
    static const QRegularExpression dispatchPattern(
        QStringLiteral("event=detail_dispatch\\s+request_id=(\\d+)\\s+detail_id=([^\\s]+)\\s+stage=start"));

    const QStringList lines = logText.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    events.reserve(lines.size());
    for (const QString& line : lines) {
        const QString trimmedLine = line.trimmed();
        const QRegularExpressionMatch resetMatch = resetPattern.match(trimmedLine);
        if (resetMatch.hasMatch()) {
            events.push_back({DetailDispatchLogEvent::Type::ResetBeforeDispatch,
                              resetMatch.captured(1).toULongLong()});
            continue;
        }

        const QRegularExpressionMatch dispatchMatch = dispatchPattern.match(trimmedLine);
        if (dispatchMatch.hasMatch()) {
            events.push_back({DetailDispatchLogEvent::Type::DispatchStart,
                              dispatchMatch.captured(1).toULongLong()});
        }
    }

    return events;
}

QJsonObject makeCanonicalRecord(const QString& id, const QString& module, const QString& title)
{
    QJsonObject identity;
    identity.insert(QStringLiteral("slug"), id.toLower());
    identity.insert(QStringLiteral("module"), module);
    identity.insert(QStringLiteral("knowledge_node"), QStringLiteral("round5-node"));

    QJsonObject meta;
    meta.insert(QStringLiteral("title"), title);
    meta.insert(QStringLiteral("difficulty"), 2);
    meta.insert(QStringLiteral("category"), QStringLiteral("round5-category"));
    meta.insert(QStringLiteral("summary"), QStringLiteral("round5 summary for %1").arg(id));
    meta.insert(QStringLiteral("is_pro"), false);

    QJsonObject plain;
    plain.insert(QStringLiteral("statement"), QStringLiteral("statement %1").arg(id));
    plain.insert(QStringLiteral("summary"), QStringLiteral("summary %1").arg(id));

    QJsonObject content;
    content.insert(QStringLiteral("render_schema_version"), 2);
    content.insert(QStringLiteral("primary_formula"), QStringLiteral("x+y>=2sqrt(xy)"));
    content.insert(QStringLiteral("plain"), plain);

    QJsonObject record;
    record.insert(QStringLiteral("id"), id);
    record.insert(QStringLiteral("schema_version"), 2);
    record.insert(QStringLiteral("type"), QStringLiteral("conclusion"));
    record.insert(QStringLiteral("status"), QStringLiteral("published"));
    record.insert(QStringLiteral("identity"), identity);
    record.insert(QStringLiteral("meta"), meta);
    record.insert(QStringLiteral("content"), content);
    return record;
}

class ScopedSandboxRoot final {
public:
    ScopedSandboxRoot()
        : previousCwd_(QDir::currentPath())
    {
        const QString baseDir = QDir(previousCwd_).filePath(QStringLiteral(".tmp_search_page_round5_tests"));
        if (!QDir().mkpath(baseDir)) {
            return;
        }

        rootPath_ = QDir(baseDir).filePath(
            QStringLiteral("case_%1_%2")
                .arg(QDateTime::currentMSecsSinceEpoch())
                .arg(QRandomGenerator::global()->bounded(1000000)));
        if (!QDir().mkpath(rootPath_)) {
            rootPath_.clear();
            return;
        }

        QDir root(rootPath_);
        const QStringList requiredDirs = {
            QStringLiteral("src"),
            QStringLiteral("app_resources"),
            QStringLiteral("data"),
            QStringLiteral("cache"),
            QStringLiteral("license"),
        };
        for (const QString& dir : requiredDirs) {
            if (!root.mkpath(dir)) {
                rootPath_.clear();
                return;
            }
        }

        QDir::setCurrent(rootPath_);
    }

    ~ScopedSandboxRoot()
    {
        if (!previousCwd_.isEmpty()) {
            QDir::setCurrent(previousCwd_);
        }
        if (!rootPath_.isEmpty()) {
            QDir(rootPath_).removeRecursively();
        }
    }

    bool isValid() const
    {
        return !rootPath_.isEmpty()
            && QDir(rootPath_).exists(QStringLiteral("src"))
            && QDir(rootPath_).exists(QStringLiteral("app_resources"))
            && QDir(rootPath_).exists(QStringLiteral("data"))
            && QDir(rootPath_).exists(QStringLiteral("cache"))
            && QDir(rootPath_).exists(QStringLiteral("license"));
    }

    QString path(const QString& relativePath) const
    {
        return QDir(rootPath_).filePath(relativePath);
    }

    bool installRound2IndexFixture() const
    {
        const QString sourcePath =
            QDir(testsSourceDir()).filePath(QStringLiteral("fixtures/test_backend_search_index_round2.json"));
        return copyFileStrict(sourcePath, path(QStringLiteral("data/backend_search_index.json")));
    }

    bool writeCanonicalContentFixture() const
    {
        QJsonObject root;
        root.insert(QStringLiteral("X001"),
                    makeCanonicalRecord(QStringLiteral("X001"),
                                        QStringLiteral("algebra"),
                                        QStringLiteral("Exact Match Core")));
        root.insert(QStringLiteral("X002"),
                    makeCanonicalRecord(QStringLiteral("X002"),
                                        QStringLiteral("geometry"),
                                        QStringLiteral("Prefix Geometry")));

        QFile file(path(QStringLiteral("data/canonical_content_v2.json")));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            return false;
        }
        const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
        const qint64 written = file.write(payload);
        file.close();
        return written == payload.size();
    }

private:
    QString previousCwd_;
    QString rootPath_;
};

}  // namespace

class SearchPageRound5UiTest final : public QObject {
    Q_OBJECT

private slots:
    void cleanupTestCase();

    void searchInput_clickSearch_triggersRunSearchAndSelectsFirstResult();
    void suggestionClick_usesSuggestClickSourceAndRunsSearch();
    void favoriteToggle_emitsFavoritesChangedSignalAndPersists();
    void webDetailDispatch_viewportResetLoggedBeforeEveryDispatch();
};

void SearchPageRound5UiTest::cleanupTestCase()
{
    logging::Logger::instance().shutdown();
}

void SearchPageRound5UiTest::searchInput_clickSearch_triggersRunSearchAndSelectsFirstResult()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");
    QVERIFY2(sandbox.installRound2IndexFixture(), "round2 index fixture should be copied into sandbox");
    QVERIFY2(sandbox.writeCanonicalContentFixture(), "canonical content fixture should be written");

    infrastructure::data::ConclusionIndexRepository indexRepository;
    QVERIFY(indexRepository.loadFromFile());
    domain::services::SearchService searchService(&indexRepository);
    domain::services::SuggestService suggestService(&indexRepository);
    SearchPage page(&searchService, &suggestService, nullptr, &indexRepository, nullptr, nullptr, nullptr);

    auto* queryInput = page.findChild<QLineEdit*>(QStringLiteral("searchInput"));
    auto* searchButton = page.findChild<QPushButton*>(QStringLiteral("searchButton"));
    auto* resultList = page.findChild<QListWidget*>(QStringLiteral("resultList"));
    QVERIFY(queryInput != nullptr);
    QVERIFY(searchButton != nullptr);
    QVERIFY(resultList != nullptr);

    QSignalSpy historySpy(&page, &SearchPage::historyChanged);
    QVERIFY(historySpy.isValid());

    queryInput->setText(QStringLiteral("exact term"));
    QTest::mouseClick(searchButton, Qt::LeftButton);

    QCOMPARE(historySpy.count(), 1);
    QTRY_VERIFY(resultList->count() > 0);
    QTRY_COMPARE(resultList->currentRow(), 0);
}

void SearchPageRound5UiTest::suggestionClick_usesSuggestClickSourceAndRunsSearch()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");
    QVERIFY2(sandbox.installRound2IndexFixture(), "round2 index fixture should be copied into sandbox");
    QVERIFY2(sandbox.writeCanonicalContentFixture(), "canonical content fixture should be written");

    infrastructure::data::ConclusionIndexRepository indexRepository;
    QVERIFY(indexRepository.loadFromFile());
    domain::services::SearchService searchService(&indexRepository);
    domain::services::SuggestService suggestService(&indexRepository);
    SearchPage page(&searchService, &suggestService, nullptr, &indexRepository, nullptr, nullptr, nullptr);

    auto* queryInput = page.findChild<QLineEdit*>(QStringLiteral("searchInput"));
    auto* suggestionList = page.findChild<QListWidget*>(QStringLiteral("searchSuggestionList"));
    auto* resultList = page.findChild<QListWidget*>(QStringLiteral("resultList"));
    QVERIFY(queryInput != nullptr);
    QVERIFY(suggestionList != nullptr);
    QVERIFY(resultList != nullptr);

    queryInput->setText(QStringLiteral("pre"));
    QTRY_VERIFY(suggestionList->count() > 0);

    QListWidgetItem* firstSuggestion = suggestionList->item(0);
    QVERIFY(firstSuggestion != nullptr);

    QSignalSpy historySpy(&page, &SearchPage::historyChanged);
    QVERIFY(historySpy.isValid());

    QVERIFY(QMetaObject::invokeMethod(&page,
                                      "onSuggestionClicked",
                                      Qt::DirectConnection,
                                      Q_ARG(QListWidgetItem*, firstSuggestion)));

    QCOMPARE(historySpy.count(), 1);
    QTRY_VERIFY(resultList->count() > 0);
    QTRY_COMPARE(resultList->currentRow(), 0);

    domain::repositories::HistoryRepository historyRepository;
    QVERIFY(historyRepository.load());
    const QList<domain::models::SearchHistoryItem> latest = historyRepository.recentItems(1);
    QVERIFY(!latest.isEmpty());
    QCOMPARE(latest.first().source, QStringLiteral("suggest_click"));
}

void SearchPageRound5UiTest::favoriteToggle_emitsFavoritesChangedSignalAndPersists()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");
    QVERIFY2(sandbox.installRound2IndexFixture(), "round2 index fixture should be copied into sandbox");
    QVERIFY2(sandbox.writeCanonicalContentFixture(), "canonical content fixture should be written");

    infrastructure::data::ConclusionIndexRepository indexRepository;
    QVERIFY(indexRepository.loadFromFile());
    domain::services::SearchService searchService(&indexRepository);
    domain::services::SuggestService suggestService(&indexRepository);
    SearchPage page(&searchService, &suggestService, nullptr, &indexRepository, nullptr, nullptr, nullptr);

    domain::repositories::FavoritesRepository favoritesRepository;
    QVERIFY(favoritesRepository.load());
    favoritesRepository.clear();
    QVERIFY(favoritesRepository.load());
    QCOMPARE(favoritesRepository.count(), 0);

    page.currentDetailDocId_ = QStringLiteral("X001");
    QSignalSpy favoritesSpy(&page, &SearchPage::favoritesChanged);
    QVERIFY(favoritesSpy.isValid());

    QVERIFY(QMetaObject::invokeMethod(&page, "onFavoriteButtonClicked", Qt::DirectConnection));
    QCOMPARE(favoritesSpy.count(), 1);

    QVERIFY(favoritesRepository.load());
    QVERIFY(favoritesRepository.contains(QStringLiteral("X001")));

    QVERIFY(QMetaObject::invokeMethod(&page, "onFavoriteButtonClicked", Qt::DirectConnection));
    QCOMPARE(favoritesSpy.count(), 2);

    QVERIFY(favoritesRepository.load());
    QVERIFY(!favoritesRepository.contains(QStringLiteral("X001")));
}

void SearchPageRound5UiTest::webDetailDispatch_viewportResetLoggedBeforeEveryDispatch()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");
    QVERIFY2(sandbox.installRound2IndexFixture(), "round2 index fixture should be copied into sandbox");
    QVERIFY2(sandbox.writeCanonicalContentFixture(), "canonical content fixture should be written");

    const QString logDir = sandbox.path(QStringLiteral("logs"));
    qputenv("MATH_SEARCH_LOG_DIR", QDir::toNativeSeparators(logDir).toUtf8());
    qputenv("MATH_SEARCH_LOG_TO_FILE", QByteArrayLiteral("1"));
    qputenv("MATH_SEARCH_LOG_TO_CONSOLE", QByteArrayLiteral("0"));
    qputenv("MATH_SEARCH_LOG_LEVEL", QByteArrayLiteral("debug"));
    logging::Logger::instance().shutdown();

    infrastructure::data::ConclusionIndexRepository indexRepository;
    QVERIFY(indexRepository.loadFromFile());

    domain::services::SearchService searchService(&indexRepository);
    domain::services::SuggestService suggestService(&indexRepository);
    SearchPage page(&searchService, &suggestService, nullptr, &indexRepository, nullptr, nullptr, nullptr);

    page.webDetailEnabled_ = true;
    page.detailPane_ = std::make_unique<ui::detail::DetailPane>(nullptr, nullptr, nullptr);
    QVERIFY(page.detailPane_ != nullptr);

    QString logFilePath;
    for (int i = 0; i < 150; ++i) {
        logFilePath = logging::Logger::instance().activeLogFilePath().trimmed();
        if (!logFilePath.isEmpty()) {
            break;
        }
        QTest::qWait(20);
    }
    QVERIFY2(!logFilePath.isEmpty(), "logger should expose an active log file path");

    const qint64 selectionTs = QDateTime::currentMSecsSinceEpoch();
    QJsonObject payload;
    payload.insert(QStringLiteral("state"), QStringLiteral("content"));
    payload.insert(QStringLiteral("detailId"), QStringLiteral("X001"));
    payload.insert(QStringLiteral("requestId"), 9527);
    QVERIFY(QMetaObject::invokeMethod(&page,
                                      "dispatchPayloadToWeb",
                                      Qt::DirectConnection,
                                      Q_ARG(QJsonObject, payload),
                                      Q_ARG(QString, QStringLiteral("X001")),
                                      Q_ARG(quint64, static_cast<quint64>(9527)),
                                      Q_ARG(qint64, selectionTs)));

    QString logText;
    QVector<DetailDispatchLogEvent> events;
    bool hasDispatchEvent = false;
    for (int i = 0; i < 300; ++i) {
        logText = readUtf8File(logFilePath);
        events = parseDetailDispatchLogEvents(logText);
        hasDispatchEvent = std::any_of(events.cbegin(), events.cend(), [](const DetailDispatchLogEvent& event) {
            return event.type == DetailDispatchLogEvent::Type::DispatchStart && event.requestId > 0;
        });
        if (hasDispatchEvent) {
            break;
        }
        QTest::qWait(20);
    }
    QVERIFY2(hasDispatchEvent, "detail dispatch start log should be recorded");

    QHash<quint64, int> resetSeenByRequestId;
    int positiveDispatchCount = 0;
    for (const DetailDispatchLogEvent& event : std::as_const(events)) {
        if (event.requestId == 0) {
            continue;
        }

        if (event.type == DetailDispatchLogEvent::Type::ResetBeforeDispatch) {
            resetSeenByRequestId[event.requestId] = resetSeenByRequestId.value(event.requestId, 0) + 1;
            continue;
        }

        positiveDispatchCount += 1;
        const int resetCount = resetSeenByRequestId.value(event.requestId, 0);
        QVERIFY2(resetCount > 0,
                 qPrintable(QStringLiteral("dispatch request_id=%1 should have reset-before-dispatch log").arg(event.requestId)));
    }

    QVERIFY2(positiveDispatchCount > 0, "at least one positive request_id dispatch should be asserted");
}

QTEST_MAIN(SearchPageRound5UiTest)

#include "test_search_page_round5.moc"

