#include "core/logging/logger.h"
#include "ui/detail/detail_perf_aggregator.h"

#include <QtTest/QtTest>

class DetailPerfAggregatorTest final : public QObject {
    Q_OBJECT

private slots:
    void cleanupTestCase();
    void parseExtra_tokensAreStructured();
    void phaseMapping_andKeyStageRules();
    void slowClassifier_thresholdsAreStable();
    void aggregatorLifecycle_beginFinishCancelSupersede();
};

void DetailPerfAggregatorTest::cleanupTestCase()
{
    logging::Logger::instance().shutdown();
}

void DetailPerfAggregatorTest::parseExtra_tokensAreStructured()
{
    const QVariantMap parsed = ui::detail::detailperf::parsePerfExtra(
        QStringLiteral("dt=24ms cache=hit sections=8 accepted=true katexMs=49.4 source=file:///detail.js:1089"));

    QCOMPARE(parsed.value(QStringLiteral("dt")).toLongLong(), 24);
    QCOMPARE(parsed.value(QStringLiteral("cache")).toString(), QStringLiteral("hit"));
    QCOMPARE(parsed.value(QStringLiteral("sections")).toLongLong(), 8);
    QCOMPARE(parsed.value(QStringLiteral("accepted")).toBool(), true);
    QCOMPARE(parsed.value(QStringLiteral("katexMs")).toDouble(), 49.4);
    QCOMPARE(parsed.value(QStringLiteral("source")).toString(), QStringLiteral("file:///detail.js:1089"));
}

void DetailPerfAggregatorTest::phaseMapping_andKeyStageRules()
{
    QCOMPARE(ui::detail::detailperf::toDisplayPhase(QStringLiteral("selection_received")), QStringLiteral("selected"));
    QCOMPARE(ui::detail::detailperf::toDisplayPhase(QStringLiteral("detail_payload_ready")), QStringLiteral("payload_ready"));
    QCOMPARE(ui::detail::detailperf::toDisplayPhase(QStringLiteral("render_complete")), QStringLiteral("complete"));
    QCOMPARE(ui::detail::detailperf::toDisplayPhase(QStringLiteral("request_superseded")), QStringLiteral("superseded"));

    QVERIFY(ui::detail::detailperf::isKeyStage(QStringLiteral("payload_ready")));
    QVERIFY(ui::detail::detailperf::isKeyStage(QStringLiteral("complete")));
    QVERIFY(!ui::detail::detailperf::isKeyStage(QStringLiteral("light_done")));
    QVERIFY(ui::detail::detailperf::isFinishPhase(QStringLiteral("complete")));
    QVERIFY(ui::detail::detailperf::isCancelPhase(QStringLiteral("superseded")));
}

void DetailPerfAggregatorTest::slowClassifier_thresholdsAreStable()
{
    QCOMPARE(static_cast<int>(ui::detail::detailperf::classifySlowStage(99)), static_cast<int>(ui::detail::SlowLevel::None));
    QCOMPARE(static_cast<int>(ui::detail::detailperf::classifySlowStage(100)), static_cast<int>(ui::detail::SlowLevel::Warn));
    QCOMPARE(static_cast<int>(ui::detail::detailperf::classifySlowStage(499)), static_cast<int>(ui::detail::SlowLevel::Warn));
    QCOMPARE(static_cast<int>(ui::detail::detailperf::classifySlowStage(500)), static_cast<int>(ui::detail::SlowLevel::Slow));
    QCOMPARE(static_cast<int>(ui::detail::detailperf::classifySlowStage(1999)), static_cast<int>(ui::detail::SlowLevel::Slow));
    QCOMPARE(static_cast<int>(ui::detail::detailperf::classifySlowStage(2000)),
             static_cast<int>(ui::detail::SlowLevel::Critical));
}

void DetailPerfAggregatorTest::aggregatorLifecycle_beginFinishCancelSupersede()
{
    ui::detail::DetailPerfAggregator aggregator;

    aggregator.beginRequest(QStringLiteral("I042"), 10);
    QVERIFY(aggregator.hasActiveRequest(10));
    aggregator.recordPhase(QStringLiteral("I042"), 10, QStringLiteral("detail_payload_ready"), 24, QStringLiteral("dt=24ms cache=hit"));
    aggregator.recordPhase(QStringLiteral("I042"), 10, QStringLiteral("first_meaningful_paint_dispatch"), 107, QString());
    aggregator.finishRequest(QStringLiteral("I042"), 10, QStringLiteral("render_complete"), 4285, QString());
    QVERIFY(!aggregator.hasActiveRequest(10));

    aggregator.beginRequest(QStringLiteral("I010"), 9);
    QVERIFY(aggregator.hasActiveRequest(9));
    aggregator.markSuperseded(9, QStringLiteral("I010"), 10, QStringLiteral("I042"));
    QVERIFY(!aggregator.hasActiveRequest(9));

    aggregator.beginRequest(QStringLiteral("I050"), 11);
    QVERIFY(aggregator.hasActiveRequest(11));
    aggregator.cancelRequest(QStringLiteral("I050"), 11, QStringLiteral("stale_before_payload"));
    QVERIFY(!aggregator.hasActiveRequest(11));
}

QTEST_APPLESS_MAIN(DetailPerfAggregatorTest)

#include "test_detail_perf_aggregator.moc"
