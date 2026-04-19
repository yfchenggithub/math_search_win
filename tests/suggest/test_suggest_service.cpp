#include "domain/services/suggest_service.h"

#include "infrastructure/data/conclusion_index_repository.h"
#include "shared/test_dump_utils.h"
#include "shared/test_fixture_loader.h"

#include <QtTest/QtTest>

#include <algorithm>
#include <QSet>

namespace {

QString makeFailureContext(const QString& expectation,
                           const QString& query,
                           const domain::models::SuggestOptions& options,
                           const domain::models::SuggestionResult& result)
{
    return QStringLiteral("expectation=%1; query=%2; options={%3}; result={%4}")
        .arg(expectation, query, tests::shared::describeSuggestOptions(options), tests::shared::summarizeSuggestions(result));
}

int indexOfSuggestion(const QVector<domain::models::SuggestionItem>& items, const QString& text)
{
    for (int i = 0; i < items.size(); ++i) {
        if (items.at(i).text.compare(text, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
}

int countSuggestionText(const QVector<domain::models::SuggestionItem>& items, const QString& text)
{
    int count = 0;
    for (const auto& item : items) {
        if (item.text.compare(text, Qt::CaseInsensitive) == 0) {
            ++count;
        }
    }
    return count;
}

bool hasTagCaseInsensitive(const QStringList& tags, const QString& expectedTag)
{
    for (const QString& tag : tags) {
        if (tag.compare(expectedTag, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

}  // namespace

class SuggestServiceTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void emptyQuery_returnsEmpty_data();
    void emptyQuery_returnsEmpty();

    void prefixSuggestions_basicBehavior();
    void termSupplement_worksWhenPrefixDisabled();
    void dedupStrategy_removesDuplicateTextWhenEnabled();
    void filters_moduleConstraintTakeEffect();
    void sorting_qualitySignalIsReasonable();
    void maxResults_limitsReturnedItems();
    void sameInputOrder_isStable();

    void realIndex_smoke_ifAvailable();

private:
    infrastructure::data::ConclusionIndexRepository fixtureRepository_;
    domain::services::SuggestService service_;
};

void SuggestServiceTest::initTestCase()
{
    QString errorSummary;
    const QString fixturePath = tests::shared::fixtureIndexPath();
    QVERIFY2(tests::shared::loadRepositoryFromFile(fixturePath, &fixtureRepository_, &errorSummary), qPrintable(errorSummary));
    QVERIFY2(fixtureRepository_.docCount() > 0, "fixture repository should contain docs");
    service_.setRepository(&fixtureRepository_);
}

void SuggestServiceTest::emptyQuery_returnsEmpty_data()
{
    QTest::addColumn<QString>("query");

    QTest::newRow("empty") << QString();
    QTest::newRow("whitespace") << QStringLiteral("   \t\n  ");
}

void SuggestServiceTest::emptyQuery_returnsEmpty()
{
    QFETCH(QString, query);

    domain::models::SuggestOptions options;
    const auto result = service_.suggest(query, options);
    QVERIFY2(result.total == 0 && result.items.isEmpty(),
             qPrintable(makeFailureContext(QStringLiteral("empty/blank query should return empty suggestions"),
                                           query,
                                           options,
                                           result)));
}

void SuggestServiceTest::prefixSuggestions_basicBehavior()
{
    const QString query = QStringLiteral("\u4e0d\u7b49");
    domain::models::SuggestOptions options;
    options.maxResults = 10;

    const auto result = service_.suggest(query, options);
    QVERIFY2(result.total > 0 && !result.items.isEmpty(),
             qPrintable(makeFailureContext(QStringLiteral("prefix query should return suggestions"), query, options, result)));

    const int checkCount = std::min(3, static_cast<int>(result.items.size()));
    for (int i = 0; i < checkCount; ++i) {
        const auto& item = result.items.at(i);
        const bool prefixOrContains =
            item.text.startsWith(query, Qt::CaseInsensitive) || item.text.contains(query, Qt::CaseInsensitive);
        QVERIFY2(prefixOrContains,
                 qPrintable(makeFailureContext(QStringLiteral("top suggestions should be prefix/related to query"),
                                               query,
                                               options,
                                               result)));
    }
}

void SuggestServiceTest::termSupplement_worksWhenPrefixDisabled()
{
    const QString query = QStringLiteral("\u5747\u503c");
    domain::models::SuggestOptions options;
    options.enablePrefix = false;
    options.maxResults = 10;

    const auto result = service_.suggest(query, options);
    QVERIFY2(result.total > 0,
             qPrintable(makeFailureContext(QStringLiteral("term supplement should still return suggestions"),
                                           query,
                                           options,
                                           result)));

    bool hasTermSource = false;
    bool hasExpectedText = false;
    for (const auto& item : result.items) {
        if (item.source == QStringLiteral("term_index")) {
            hasTermSource = true;
        }
        if (item.text.contains(query, Qt::CaseInsensitive)) {
            hasExpectedText = true;
        }
    }
    QVERIFY2(hasTermSource,
             qPrintable(makeFailureContext(QStringLiteral("expected at least one term_index suggestion"),
                                           query,
                                           options,
                                           result)));
    QVERIFY2(hasExpectedText,
             qPrintable(makeFailureContext(QStringLiteral("expected at least one suggestion containing query"),
                                           query,
                                           options,
                                           result)));
}

void SuggestServiceTest::dedupStrategy_removesDuplicateTextWhenEnabled()
{
    const QString query = QStringLiteral("\u4e0d\u7b49\u5f0f");

    domain::models::SuggestOptions dedupOn;
    dedupOn.enableExactDedup = true;
    dedupOn.maxResults = 30;
    const auto deduped = service_.suggest(query, dedupOn);

    domain::models::SuggestOptions dedupOff = dedupOn;
    dedupOff.enableExactDedup = false;
    const auto undeduped = service_.suggest(query, dedupOff);

    const QString targetTextA = QStringLiteral("\u4e0d\u7b49\u5f0f");
    const QString targetTextB = QStringLiteral("\u4e0d\u7b49\u5f0f\u6280\u5de7");

    QVERIFY2(countSuggestionText(deduped.items, targetTextA) <= 1 && countSuggestionText(deduped.items, targetTextB) <= 1,
             qPrintable(makeFailureContext(QStringLiteral("dedup enabled should not keep repeated suggestion text"),
                                           query,
                                           dedupOn,
                                           deduped)));
    QVERIFY2(countSuggestionText(undeduped.items, targetTextA) >= 2 || countSuggestionText(undeduped.items, targetTextB) >= 2,
             qPrintable(makeFailureContext(QStringLiteral("dedup disabled should expose repeated text from multiple sources"),
                                           query,
                                           dedupOff,
                                           undeduped)));
}

void SuggestServiceTest::filters_moduleConstraintTakeEffect()
{
    const QString query = QStringLiteral("\u4e0d\u7b49");

    domain::models::SuggestOptions baseOptions;
    baseOptions.maxResults = 20;
    const auto baseResult = service_.suggest(query, baseOptions);
    QVERIFY2(baseResult.total > 0, "base suggest query should not be empty");

    domain::models::SuggestOptions geometryFilter = baseOptions;
    geometryFilter.moduleFilter = {QStringLiteral("geometry")};
    const auto geometryResult = service_.suggest(query, geometryFilter);
    QVERIFY2(geometryResult.total > 0 && geometryResult.total < baseResult.total,
             qPrintable(makeFailureContext(QStringLiteral("module filter should shrink suggestion set"),
                                           query,
                                           geometryFilter,
                                           geometryResult)));

    for (const auto& item : geometryResult.items) {
        for (const QString& docId : item.targetDocIds) {
            const auto* doc = fixtureRepository_.getDocById(docId);
            QVERIFY2(doc != nullptr,
                     qPrintable(makeFailureContext(QStringLiteral("target doc id should exist in repository"),
                                                   query,
                                                   geometryFilter,
                                                   geometryResult)));
            QVERIFY2(doc->module.compare(QStringLiteral("geometry"), Qt::CaseInsensitive) == 0,
                     qPrintable(makeFailureContext(QStringLiteral("all target docs should satisfy geometry module filter"),
                                                   query,
                                                   geometryFilter,
                                                   geometryResult)));
        }
    }

    const QString categoryQuery = QStringLiteral("\u51fd\u6570");
    const auto categoryBaseResult = service_.suggest(categoryQuery, baseOptions);
    QVERIFY2(categoryBaseResult.total > 0,
             qPrintable(makeFailureContext(QStringLiteral("category baseline query should return suggestions"),
                                           categoryQuery,
                                           baseOptions,
                                           categoryBaseResult)));

    domain::models::SuggestOptions categoryFilter = baseOptions;
    categoryFilter.categoryFilter = {QStringLiteral("InEquality")};
    const auto categoryResult = service_.suggest(categoryQuery, categoryFilter);
    QVERIFY2(categoryResult.total < categoryBaseResult.total,
             qPrintable(makeFailureContext(QStringLiteral("category filter should change result set for categoryQuery"),
                                           categoryQuery,
                                           categoryFilter,
                                           categoryResult)));
    for (const auto& item : categoryResult.items) {
        for (const QString& docId : item.targetDocIds) {
            const auto* doc = fixtureRepository_.getDocById(docId);
            QVERIFY2(doc != nullptr,
                     qPrintable(makeFailureContext(QStringLiteral("target doc id should exist in repository"),
                                                   categoryQuery,
                                                   categoryFilter,
                                                   categoryResult)));
            QVERIFY2(doc->category.compare(QStringLiteral("inequality"), Qt::CaseInsensitive) == 0,
                     qPrintable(makeFailureContext(QStringLiteral("all target docs should satisfy inequality category filter"),
                                                   categoryQuery,
                                                   categoryFilter,
                                                   categoryResult)));
        }
    }

    domain::models::SuggestOptions tagFilter = baseOptions;
    tagFilter.tagFilter = {QStringLiteral("\u5747\u503c\u4e0d\u7b49\u5f0f")};
    const auto tagResult = service_.suggest(query, tagFilter);
    QVERIFY2(tagResult.total > 0 && tagResult.total < baseResult.total,
             qPrintable(makeFailureContext(QStringLiteral("tag filter should shrink suggestion set"), query, tagFilter, tagResult)));
    for (const auto& item : tagResult.items) {
        for (const QString& docId : item.targetDocIds) {
            const auto* doc = fixtureRepository_.getDocById(docId);
            QVERIFY2(doc != nullptr,
                     qPrintable(makeFailureContext(QStringLiteral("target doc id should exist in repository"),
                                                   query,
                                                   tagFilter,
                                                   tagResult)));
            QVERIFY2(hasTagCaseInsensitive(doc->tags, QStringLiteral("\u5747\u503c\u4e0d\u7b49\u5f0f")),
                     qPrintable(makeFailureContext(QStringLiteral("all target docs should satisfy tag filter"),
                                                   query,
                                                   tagFilter,
                                                   tagResult)));
        }
    }
}

void SuggestServiceTest::sorting_qualitySignalIsReasonable()
{
    const QString query = QStringLiteral("\u4e0d\u7b49");
    domain::models::SuggestOptions options;
    options.maxResults = 20;

    const auto result = service_.suggest(query, options);
    QVERIFY2(result.total > 0, "sorting test needs non-empty result");

    const int titleIndex = indexOfSuggestion(result.items, QStringLiteral("\u4e0d\u7b49\u9898\u578b"));
    const int tagIndex = indexOfSuggestion(result.items, QStringLiteral("\u4e0d\u7b49\u6280\u5de7"));
    QVERIFY2(titleIndex >= 0 && tagIndex >= 0,
             qPrintable(makeFailureContext(QStringLiteral("fixture should expose quality-comparison suggestions"),
                                           query,
                                           options,
                                           result)));
    QVERIFY2(titleIndex < tagIndex,
             qPrintable(makeFailureContext(QStringLiteral("title-backed suggestion should rank ahead of tag-only suggestion"),
                                           query,
                                           options,
                                           result)));

    for (int i = 1; i < result.items.size(); ++i) {
        QVERIFY2(result.items.at(i - 1).score + 1e-9 >= result.items.at(i).score,
                 qPrintable(makeFailureContext(QStringLiteral("scores should be non-increasing after sort"),
                                               query,
                                               options,
                                               result)));
    }
}

void SuggestServiceTest::maxResults_limitsReturnedItems()
{
    const QString query = QStringLiteral("\u4e0d\u7b49");

    domain::models::SuggestOptions options2;
    options2.maxResults = 2;
    const auto result2 = service_.suggest(query, options2);
    QVERIFY2(result2.items.size() <= 2,
             qPrintable(makeFailureContext(QStringLiteral("maxResults=2 should cap returned items"), query, options2, result2)));
    QVERIFY2(result2.total >= result2.items.size(),
             qPrintable(makeFailureContext(QStringLiteral("total should be >= returned size"), query, options2, result2)));

    domain::models::SuggestOptions options5 = options2;
    options5.maxResults = 5;
    const auto result5 = service_.suggest(query, options5);
    QVERIFY2(result5.items.size() <= 5,
             qPrintable(makeFailureContext(QStringLiteral("maxResults=5 should cap returned items"), query, options5, result5)));
    QVERIFY2(result2.total == result5.total,
             qPrintable(makeFailureContext(QStringLiteral("total should be independent from maxResults truncation"),
                                           query,
                                           options5,
                                           result5)));
}

void SuggestServiceTest::sameInputOrder_isStable()
{
    const QString query = QStringLiteral("\u4e0d\u7b49");
    domain::models::SuggestOptions options;
    options.maxResults = 8;

    const auto runA = service_.suggest(query, options);
    const auto runB = service_.suggest(query, options);
    const auto runC = service_.suggest(query, options);
    QVERIFY2(runA.total > 0, "stability test requires non-empty baseline result");

    const int topN = std::min({5,
                               static_cast<int>(runA.items.size()),
                               static_cast<int>(runB.items.size()),
                               static_cast<int>(runC.items.size())});
    QVERIFY2(topN > 0, "stability test requires comparable items");
    for (int i = 0; i < topN; ++i) {
        const auto& a = runA.items.at(i);
        const auto& b = runB.items.at(i);
        const auto& c = runC.items.at(i);
        QVERIFY2(a.text == b.text && a.text == c.text && a.source == b.source && a.source == c.source,
                 qPrintable(makeFailureContext(QStringLiteral("top-N suggestion order/source should be stable"),
                                               query,
                                               options,
                                               runA)));
    }
}

void SuggestServiceTest::realIndex_smoke_ifAvailable()
{
    infrastructure::data::ConclusionIndexRepository realRepository;
    QString errorSummary;
    const QString path = tests::shared::realIndexPath();
    if (!tests::shared::loadRepositoryFromFile(path, &realRepository, &errorSummary)) {
        QSKIP(qPrintable(QStringLiteral("skip real-index smoke: %1").arg(errorSummary)));
    }

    domain::services::SuggestService realService(&realRepository);
    domain::models::SuggestOptions options;
    options.maxResults = 12;

    const QStringList probeQueries = {
        QStringLiteral("de"),
        QStringLiteral("deng"),
        QStringLiteral("\u4e0d\u7b49")
    };

    QString usedQuery;
    domain::models::SuggestionResult firstRun;
    for (const QString& query : probeQueries) {
        const auto candidate = realService.suggest(query, options);
        if (candidate.total > 0) {
            usedQuery = query;
            firstRun = candidate;
            break;
        }
    }

    if (firstRun.total == 0) {
        QSKIP("real index loaded but probe queries returned empty; skip unstable environment");
    }

    QVERIFY2(firstRun.total >= firstRun.items.size(),
             qPrintable(makeFailureContext(QStringLiteral("real-index smoke expects total>=returned"),
                                           usedQuery,
                                           options,
                                           firstRun)));

    QSet<QString> dedupe;
    for (const auto& item : firstRun.items) {
        QVERIFY2(!dedupe.contains(item.normalizedText),
                 qPrintable(makeFailureContext(QStringLiteral("real-index smoke expects deduplicated suggestion text"),
                                               usedQuery,
                                               options,
                                               firstRun)));
        dedupe.insert(item.normalizedText);
    }

    const auto secondRun = realService.suggest(usedQuery, options);
    const int topN = std::min({5, static_cast<int>(firstRun.items.size()), static_cast<int>(secondRun.items.size())});
    QVERIFY2(topN > 0, "real-index smoke requires comparable top-N suggestions");
    for (int i = 0; i < topN; ++i) {
        const auto& a = firstRun.items.at(i);
        const auto& b = secondRun.items.at(i);
        QVERIFY2(a.text == b.text && a.source == b.source,
                 qPrintable(makeFailureContext(QStringLiteral("real-index top-N suggestions should stay stable"),
                                               usedQuery,
                                               options,
                                               firstRun)));
    }
}

QTEST_APPLESS_MAIN(SuggestServiceTest)

#include "test_suggest_service.moc"
