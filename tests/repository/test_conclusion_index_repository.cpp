#include "infrastructure/data/conclusion_index_repository.h"
#include "domain/models/search_result_models.h"

#include "shared/test_fixture_loader.h"

#include <QJsonParseError>
#include <QtTest/QtTest>

namespace {

bool hasPostingDocId(const QVector<domain::models::PostingEntry>& postings, const QString& expectedDocId)
{
    for (const auto& posting : postings) {
        if (posting.docId == expectedDocId) {
            return true;
        }
    }
    return false;
}

}  // namespace

class ConclusionIndexRepositoryTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void findTerm_returnsPostingEntries();
    void findPrefix_returnsPostingEntries();
    void getDocById_returnsDocRecord();
    void missingDocId_returnsNullptr();
    void malformedIndex_toleratesAndSkipsInvalidRows();
    void domainTopicMap_validFile_loadsAndBuildsLookup();
    void domainTopicMap_objectStyleDocsField_loadsAndBuildsLookup();
    void domainTopicMap_missingFile_degradesGracefully();
    void domainTopicMap_malformedFile_degradesGracefully();

private:
    infrastructure::data::ConclusionIndexRepository repositoryRound2_;
};

void ConclusionIndexRepositoryTest::initTestCase()
{
    QString errorSummary;
    QVERIFY2(
        tests::shared::loadRepositoryFromFile(tests::shared::fixtureIndexRound2Path(), &repositoryRound2_, &errorSummary),
        qPrintable(errorSummary));
    QVERIFY2(repositoryRound2_.docCount() > 0, "round2 fixture repository should contain docs");
}

void ConclusionIndexRepositoryTest::findTerm_returnsPostingEntries()
{
    const auto* postings = repositoryRound2_.findTerm(QStringLiteral("exact term"));
    QVERIFY(postings != nullptr);
    QVERIFY2(hasPostingDocId(*postings, QStringLiteral("X001")), "term posting should include X001");
}

void ConclusionIndexRepositoryTest::findPrefix_returnsPostingEntries()
{
    const auto* postings = repositoryRound2_.findPrefix(QStringLiteral("pre"));
    QVERIFY(postings != nullptr);
    QVERIFY2(hasPostingDocId(*postings, QStringLiteral("X002")), "prefix posting should include X002");
}

void ConclusionIndexRepositoryTest::getDocById_returnsDocRecord()
{
    const auto* doc = repositoryRound2_.getDocById(QStringLiteral("X001"));
    QVERIFY(doc != nullptr);
    QCOMPARE(doc->id, QStringLiteral("X001"));
    QCOMPARE(doc->title, QStringLiteral("Exact Match Core"));
    QCOMPARE(doc->module, QStringLiteral("algebra"));
}

void ConclusionIndexRepositoryTest::missingDocId_returnsNullptr()
{
    QVERIFY(repositoryRound2_.getDocById(QStringLiteral("NOT_FOUND")) == nullptr);
    QVERIFY(repositoryRound2_.getDocById(QStringLiteral("   ")) == nullptr);
}

void ConclusionIndexRepositoryTest::malformedIndex_toleratesAndSkipsInvalidRows()
{
    infrastructure::data::ConclusionIndexRepository malformedRepository;
    QString errorSummary;
    QVERIFY2(tests::shared::loadRepositoryFromFile(
                 tests::shared::malformedFixtureIndexPath(), &malformedRepository, &errorSummary),
             qPrintable(errorSummary));

    const auto& diagnostics = malformedRepository.diagnostics();
    QVERIFY2(!diagnostics.warnings.isEmpty(), "malformed fixture should trigger parse warnings");
    QVERIFY2(diagnostics.skippedDocCount > 0, "malformed fixture should skip invalid docs");
    QVERIFY2(diagnostics.skippedPostingCount > 0, "malformed fixture should skip invalid postings");

    const auto* doc = malformedRepository.getDocById(QStringLiteral("M001"));
    QVERIFY(doc != nullptr);
    QCOMPARE(doc->id, QStringLiteral("M001"));
    QCOMPARE(doc->module, QStringLiteral("123"));

    const auto* termPostings = malformedRepository.findTerm(QStringLiteral("mal_key"));
    QVERIFY(termPostings != nullptr);
    QCOMPARE(termPostings->size(), 1);
    QCOMPARE(termPostings->at(0).docId, QStringLiteral("M001"));

    QVERIFY(malformedRepository.findTerm(QStringLiteral("all_bad")) == nullptr);

    const auto* prefixPostings = malformedRepository.findPrefix(QStringLiteral("mal_pre"));
    QVERIFY(prefixPostings != nullptr);
    QCOMPARE(prefixPostings->size(), 1);
    QCOMPARE(prefixPostings->at(0).docId, QStringLiteral("M001"));

    QVERIFY(malformedRepository.findPrefix(QStringLiteral("all_bad_pre")) == nullptr);
}

void ConclusionIndexRepositoryTest::domainTopicMap_validFile_loadsAndBuildsLookup()
{
    const QString mapPath = tests::shared::fixtureDomainTopicMapPath();
    const bool loaded = repositoryRound2_.loadDomainTopicMap(mapPath);
    QVERIFY2(loaded, "valid domain_topic_map fixture should load");
    QVERIFY(repositoryRound2_.hasDomainTopicMap());

    const auto& map = repositoryRound2_.domainTopicMap();
    QVERIFY2(!map.domains.isEmpty(), "domain list should not be empty");
    QVERIFY2(!map.domainByAlias.isEmpty(), "domain alias lookup should be built");
    QVERIFY2(!map.topicByAlias.isEmpty(), "topic alias lookup should be built");

    const auto& diag = repositoryRound2_.domainTopicMapDiagnostics();
    QVERIFY(diag.fileExists);
    QCOMPARE(diag.parseError.error, QJsonParseError::NoError);
    QVERIFY(diag.domainCount >= 1);
    QVERIFY(diag.topicCount >= 1);
    QVERIFY(diag.aliasCount >= 1);
}

void ConclusionIndexRepositoryTest::domainTopicMap_objectStyleDocsField_loadsAndBuildsLookup()
{
    const QString mapPath = tests::shared::fixtureDomainTopicMapObjectDocsPath();
    const bool loaded = repositoryRound2_.loadDomainTopicMap(mapPath);
    QVERIFY2(loaded, "object-style domain_topic_map fixture should load");
    QVERIFY(repositoryRound2_.hasDomainTopicMap());

    const auto& map = repositoryRound2_.domainTopicMap();
    QVERIFY2(!map.domains.isEmpty(), "object-style domain list should not be empty");
    QVERIFY2(!map.domainByAlias.isEmpty(), "object-style domain alias lookup should be built");
    QVERIFY2(!map.topicByAlias.isEmpty(), "object-style topic alias lookup should be built");

    const QString normalizedDomainAlias = domain::models::normalizeQueryText(QStringLiteral("\u4e0d\u7b49"));
    QVERIFY2(map.domainByAlias.contains(normalizedDomainAlias), "domain alias lookup should include normalized alias");
    const QString normalizedTopicAlias = domain::models::normalizeQueryText(QStringLiteral("\u67ef\u897f"));
    QVERIFY2(map.topicByAlias.contains(normalizedTopicAlias), "topic alias lookup should include normalized alias");

    const auto& diag = repositoryRound2_.domainTopicMapDiagnostics();
    QVERIFY(diag.fileExists);
    QCOMPARE(diag.parseError.error, QJsonParseError::NoError);
    QVERIFY(diag.domainCount >= 1);
    QVERIFY(diag.topicCount >= 1);
    QVERIFY(diag.aliasCount >= 1);
}

void ConclusionIndexRepositoryTest::domainTopicMap_missingFile_degradesGracefully()
{
    const QString missingPath = tests::shared::testsSourceDir() + QStringLiteral("/fixtures/not_exists_domain_topic_map.json");
    const bool loaded = repositoryRound2_.loadDomainTopicMap(missingPath);
    QVERIFY2(!loaded, "missing domain_topic_map should return false");
    QVERIFY(!repositoryRound2_.hasDomainTopicMap());

    const auto& diag = repositoryRound2_.domainTopicMapDiagnostics();
    QVERIFY(!diag.fileExists);
}

void ConclusionIndexRepositoryTest::domainTopicMap_malformedFile_degradesGracefully()
{
    const bool loaded = repositoryRound2_.loadDomainTopicMap(tests::shared::malformedFixtureDomainTopicMapPath());
    QVERIFY2(!loaded, "malformed domain_topic_map should return false");
    QVERIFY(!repositoryRound2_.hasDomainTopicMap());

    const auto& diag = repositoryRound2_.domainTopicMapDiagnostics();
    QVERIFY(diag.fileExists);
    QVERIFY(diag.parseError.error != QJsonParseError::NoError);
}

QTEST_APPLESS_MAIN(ConclusionIndexRepositoryTest)

#include "test_conclusion_index_repository.moc"
