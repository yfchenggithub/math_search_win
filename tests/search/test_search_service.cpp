#include "domain/services/search_service.h"

#include "infrastructure/data/conclusion_index_repository.h"
#include "shared/test_dump_utils.h"
#include "shared/test_fixture_loader.h"

#include <QtTest/QtTest>

#include <algorithm>
#include <QSet>

namespace {

QString makeFailureContext(const QString& expectation,
                           const QString& query,
                           const domain::models::SearchOptions& options,
                           const domain::models::SearchResult& result)
{
    return QStringLiteral("expectation=%1; query=%2; options={%3}; result={%4}")
        .arg(expectation, query, tests::shared::describeSearchOptions(options), tests::shared::summarizeSearchHits(result));
}

int indexOfDocId(const QVector<domain::models::SearchHit>& hits, const QString& docId)
{
    for (int i = 0; i < hits.size(); ++i) {
        if (hits.at(i).docId == docId) {
            return i;
        }
    }
    return -1;
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

class SearchServiceTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void emptyQuery_returnsEmpty_data();
    void emptyQuery_returnsEmpty();

    void basicHit_termAndPrefixChinese();
    void filters_moduleCategoryTag_takeEffect();
    void prefixAndTermCombined_hasNoDuplicateDocs();
    void sameInputOrder_isStable();
    void exactBoost_promotesTermMatchedDocs();
    void noResultQuery_returnsEmptyAndSafe();
    void maxResults_limitsReturnedItemsButKeepsTotal();

    void realIndex_smoke_ifAvailable();

private:
    infrastructure::data::ConclusionIndexRepository fixtureRepository_;
    domain::services::SearchService service_;
};

void SearchServiceTest::initTestCase()
{
    QString errorSummary;
    const QString fixturePath = tests::shared::fixtureIndexPath();
    QVERIFY2(tests::shared::loadRepositoryFromFile(fixturePath, &fixtureRepository_, &errorSummary), qPrintable(errorSummary));
    QVERIFY2(fixtureRepository_.docCount() > 0, "fixture repository should contain docs");
    service_.setRepository(&fixtureRepository_);
}

void SearchServiceTest::emptyQuery_returnsEmpty_data()
{
    QTest::addColumn<QString>("query");

    QTest::newRow("empty") << QString();
    QTest::newRow("whitespace") << QStringLiteral("   \t\n  ");
}

void SearchServiceTest::emptyQuery_returnsEmpty()
{
    QFETCH(QString, query);

    domain::models::SearchOptions options;
    const domain::models::SearchResult result = service_.search(query, options);

    QVERIFY2(result.total == 0 && result.hits.isEmpty(),
             qPrintable(makeFailureContext(QStringLiteral("empty/blank query should return empty result"), query, options, result)));
}

void SearchServiceTest::basicHit_termAndPrefixChinese()
{
    const QString query = QStringLiteral("\u4e0d\u7b49\u5f0f");
    domain::models::SearchOptions options;

    const domain::models::SearchResult result = service_.search(query, options);
    QVERIFY2(result.total > 0 && !result.hits.isEmpty(),
             qPrintable(makeFailureContext(QStringLiteral("query should hit at least one result"), query, options, result)));
    QVERIFY2(result.total >= result.hits.size(),
             qPrintable(makeFailureContext(QStringLiteral("result.total should be >= returned hits"), query, options, result)));
    QVERIFY2(indexOfDocId(result.hits, QStringLiteral("A001")) >= 0,
             qPrintable(makeFailureContext(QStringLiteral("fixture term hit A001 should appear"), query, options, result)));
}

void SearchServiceTest::filters_moduleCategoryTag_takeEffect()
{
    const QString query = QStringLiteral("\u4e0d\u7b49\u5f0f");

    domain::models::SearchOptions baseOptions;
    const auto baseResult = service_.search(query, baseOptions);
    QVERIFY2(baseResult.total > 0, "fixture baseline query should not be empty");

    domain::models::SearchOptions moduleOptions;
    moduleOptions.moduleFilter = {QStringLiteral("geometry")};
    const auto moduleResult = service_.search(query, moduleOptions);
    QVERIFY2(moduleResult.total > 0 && moduleResult.total < baseResult.total,
             qPrintable(makeFailureContext(QStringLiteral("module filter should shrink result set"), query, moduleOptions, moduleResult)));
    for (const auto& hit : moduleResult.hits) {
        QVERIFY2(hit.module.compare(QStringLiteral("geometry"), Qt::CaseInsensitive) == 0,
                 qPrintable(makeFailureContext(QStringLiteral("all module-filtered hits must be geometry"),
                                               query,
                                               moduleOptions,
                                               moduleResult)));
    }

    domain::models::SearchOptions categoryOptions;
    categoryOptions.categoryFilter = {QStringLiteral("InEquality")};
    const auto categoryResult = service_.search(query, categoryOptions);
    QVERIFY2(categoryResult.total > 0 && categoryResult.total < baseResult.total,
             qPrintable(makeFailureContext(QStringLiteral("category filter should shrink result set"),
                                           query,
                                           categoryOptions,
                                           categoryResult)));
    for (const auto& hit : categoryResult.hits) {
        QVERIFY2(hit.category.compare(QStringLiteral("inequality"), Qt::CaseInsensitive) == 0,
                 qPrintable(makeFailureContext(QStringLiteral("all category-filtered hits must match inequality"),
                                               query,
                                               categoryOptions,
                                               categoryResult)));
    }

    domain::models::SearchOptions tagOptions;
    tagOptions.tagFilter = {QStringLiteral("\u5747\u503c\u4e0d\u7b49\u5f0f")};
    const auto tagResult = service_.search(query, tagOptions);
    QVERIFY2(tagResult.total > 0 && tagResult.total < baseResult.total,
             qPrintable(makeFailureContext(QStringLiteral("tag filter should shrink result set"), query, tagOptions, tagResult)));
    for (const auto& hit : tagResult.hits) {
        QVERIFY2(hasTagCaseInsensitive(hit.tags, QStringLiteral("\u5747\u503c\u4e0d\u7b49\u5f0f")),
                 qPrintable(makeFailureContext(QStringLiteral("all tag-filtered hits must contain target tag"),
                                               query,
                                               tagOptions,
                                               tagResult)));
    }
}

void SearchServiceTest::prefixAndTermCombined_hasNoDuplicateDocs()
{
    const QString query = QStringLiteral("\u4e0d\u7b49\u5f0f");
    domain::models::SearchOptions options;
    options.enableTerm = true;
    options.enablePrefix = true;
    options.maxResults = 32;

    const auto result = service_.search(query, options);
    QVERIFY2(result.total > 0, "combined prefix+term query should return hits");

    QSet<QString> docIds;
    for (const auto& hit : result.hits) {
        QVERIFY2(!docIds.contains(hit.docId),
                 qPrintable(makeFailureContext(QStringLiteral("combined prefix+term should not duplicate doc ids"),
                                               query,
                                               options,
                                               result)));
        docIds.insert(hit.docId);
    }

    QVERIFY2(indexOfDocId(result.hits, QStringLiteral("A005")) >= 0,
             qPrintable(makeFailureContext(QStringLiteral("prefix-only doc A005 should appear"), query, options, result)));
    QVERIFY2(indexOfDocId(result.hits, QStringLiteral("A001")) >= 0,
             qPrintable(makeFailureContext(QStringLiteral("term-matched doc A001 should appear"), query, options, result)));
}

void SearchServiceTest::sameInputOrder_isStable()
{
    const QString query = QStringLiteral("\u4e0d\u7b49\u5f0f");
    domain::models::SearchOptions options;
    options.maxResults = 10;

    const auto runA = service_.search(query, options);
    const auto runB = service_.search(query, options);
    const auto runC = service_.search(query, options);

    QVERIFY2(runA.total > 0, "stability test requires non-empty baseline result");
    const int topN = std::min({5,
                               static_cast<int>(runA.hits.size()),
                               static_cast<int>(runB.hits.size()),
                               static_cast<int>(runC.hits.size())});
    QVERIFY2(topN > 0, "stability test requires at least one comparable hit");

    for (int i = 0; i < topN; ++i) {
        QVERIFY2(runA.hits.at(i).docId == runB.hits.at(i).docId && runA.hits.at(i).docId == runC.hits.at(i).docId,
                 qPrintable(makeFailureContext(QStringLiteral("top-N order must be stable across repeated calls"),
                                               query,
                                               options,
                                               runA)));
    }
}

void SearchServiceTest::exactBoost_promotesTermMatchedDocs()
{
    const QString query = QStringLiteral("\u4e0d\u7b49\u5f0f");

    domain::models::SearchOptions withoutBoost;
    withoutBoost.enableExactBoost = false;
    withoutBoost.maxResults = 16;
    const auto noBoostResult = service_.search(query, withoutBoost);

    domain::models::SearchOptions withBoost = withoutBoost;
    withBoost.enableExactBoost = true;
    const auto boostResult = service_.search(query, withBoost);

    const int noBoostA005 = indexOfDocId(noBoostResult.hits, QStringLiteral("A005"));
    const int noBoostA001 = indexOfDocId(noBoostResult.hits, QStringLiteral("A001"));
    QVERIFY2(noBoostA005 >= 0 && noBoostA001 >= 0,
             qPrintable(makeFailureContext(QStringLiteral("no-boost result should contain A005 and A001"),
                                           query,
                                           withoutBoost,
                                           noBoostResult)));
    QVERIFY2(noBoostA005 < noBoostA001,
             qPrintable(makeFailureContext(QStringLiteral("without exact boost, high prefix-only doc A005 should rank above A001"),
                                           query,
                                           withoutBoost,
                                           noBoostResult)));

    const int boostA005 = indexOfDocId(boostResult.hits, QStringLiteral("A005"));
    const int boostA001 = indexOfDocId(boostResult.hits, QStringLiteral("A001"));
    QVERIFY2(boostA005 >= 0 && boostA001 >= 0,
             qPrintable(makeFailureContext(QStringLiteral("boosted result should contain A005 and A001"),
                                           query,
                                           withBoost,
                                           boostResult)));
    QVERIFY2(boostA001 < boostA005,
             qPrintable(makeFailureContext(QStringLiteral("with exact boost, term-matched A001 should outrank A005"),
                                           query,
                                           withBoost,
                                           boostResult)));
}

void SearchServiceTest::noResultQuery_returnsEmptyAndSafe()
{
    const QString query = QStringLiteral("__definitely_not_found__");
    domain::models::SearchOptions options;

    const auto result = service_.search(query, options);
    QVERIFY2(result.total == 0 && result.hits.isEmpty(),
             qPrintable(makeFailureContext(QStringLiteral("unknown query should return empty without crash"), query, options, result)));
}

void SearchServiceTest::maxResults_limitsReturnedItemsButKeepsTotal()
{
    const QString query = QStringLiteral("\u4e0d\u7b49\u5f0f");
    domain::models::SearchOptions options;
    options.maxResults = 2;

    const auto result = service_.search(query, options);
    QVERIFY2(result.hits.size() <= 2,
             qPrintable(makeFailureContext(QStringLiteral("returned hits should respect maxResults"), query, options, result)));
    QVERIFY2(result.total >= result.hits.size(),
             qPrintable(makeFailureContext(QStringLiteral("total should be >= returned size"), query, options, result)));
    QVERIFY2(result.total > result.hits.size(),
             qPrintable(makeFailureContext(QStringLiteral("fixture query should be truncated by maxResults"), query, options, result)));
}

void SearchServiceTest::realIndex_smoke_ifAvailable()
{
    infrastructure::data::ConclusionIndexRepository realRepository;
    QString errorSummary;
    const QString path = tests::shared::realIndexPath();
    if (!tests::shared::loadRepositoryFromFile(path, &realRepository, &errorSummary)) {
        QSKIP(qPrintable(QStringLiteral("skip real-index smoke: %1").arg(errorSummary)));
    }

    domain::services::SearchService realService(&realRepository);
    domain::models::SearchOptions options;
    options.maxResults = 12;

    const QStringList probeQueries = {
        QStringLiteral("de"),
        QStringLiteral("deng"),
        QStringLiteral("\u4e0d\u7b49\u5f0f")
    };

    QString usedQuery;
    domain::models::SearchResult firstRun;
    for (const QString& query : probeQueries) {
        const auto candidate = realService.search(query, options);
        if (candidate.total > 0) {
            usedQuery = query;
            firstRun = candidate;
            break;
        }
    }

    if (firstRun.total == 0) {
        QSKIP("real index loaded but probe queries returned empty; skip unstable environment");
    }

    QVERIFY2(firstRun.total >= firstRun.hits.size(),
             qPrintable(makeFailureContext(QStringLiteral("real-index smoke expects total>=returned"), usedQuery, options, firstRun)));

    QSet<QString> dedupe;
    for (const auto& hit : firstRun.hits) {
        QVERIFY2(!dedupe.contains(hit.docId),
                 qPrintable(makeFailureContext(QStringLiteral("real-index smoke expects no duplicate doc ids"),
                                               usedQuery,
                                               options,
                                               firstRun)));
        dedupe.insert(hit.docId);
    }

    const auto secondRun = realService.search(usedQuery, options);
    const int topN = std::min({5, static_cast<int>(firstRun.hits.size()), static_cast<int>(secondRun.hits.size())});
    QVERIFY2(topN > 0, "real-index smoke requires comparable top-N hits");
    for (int i = 0; i < topN; ++i) {
        QVERIFY2(firstRun.hits.at(i).docId == secondRun.hits.at(i).docId,
                 qPrintable(makeFailureContext(QStringLiteral("real-index top-N order should be stable"),
                                               usedQuery,
                                               options,
                                               firstRun)));
    }
}

QTEST_APPLESS_MAIN(SearchServiceTest)

#include "test_search_service.moc"
