#include "infrastructure/data/conclusion_index_repository.h"

#include "shared/test_fixture_loader.h"

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

QTEST_APPLESS_MAIN(ConclusionIndexRepositoryTest)

#include "test_conclusion_index_repository.moc"
