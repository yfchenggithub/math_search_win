#include "core/logging/logger.h"
#include "license/feature_gate.h"
#include "license/license_state.h"

#include <QtTest/QtTest>

namespace {

license::LicenseState buildFullState(const QStringList& enabled)
{
    license::LicenseState state;
    state.status = license::LicenseStatus::ValidFull;
    state.isTrial = false;
    state.isFull = true;
    state.licenseSerial = QStringLiteral("LIC-2026-8888");
    state.enabledFeatures = enabled;
    return state;
}

license::LicenseState buildTrialState(license::LicenseStatus status = license::LicenseStatus::Missing)
{
    license::LicenseState state;
    state.status = status;
    state.isTrial = true;
    state.isFull = false;
    state.enabledFeatures = {
        QStringLiteral("basic_search_preview"),
        QStringLiteral("full_search"),
        QStringLiteral("full_detail"),
    };
    return state;
}

void assertFeatureMatrix(const license::FeatureGate& gate,
                         bool basicPreview,
                         bool fullSearch,
                         bool fullDetail,
                         bool favorites,
                         bool advancedFilter)
{
    QCOMPARE(gate.isEnabled(license::Feature::BasicSearchPreview), basicPreview);
    QCOMPARE(gate.isEnabled(license::Feature::FullSearch), fullSearch);
    QCOMPARE(gate.isEnabled(license::Feature::FullDetail), fullDetail);
    QCOMPARE(gate.isEnabled(license::Feature::Favorites), favorites);
    QCOMPARE(gate.isEnabled(license::Feature::AdvancedFilter), advancedFilter);
}

}  // namespace

class FeatureGateTest final : public QObject {
    Q_OBJECT

private slots:
    void cleanupTestCase();

    void defaultState_isTrialAndPreviewOnly();
    void setLicenseState_fullState_syncsAndRespectsConfiguredFeatures();
    void setLicenseState_trialState_forcesTrialFeatureMatrix();
    void setLicenseState_fullStateWithoutConfiguredFeatures_fallsBackToFullDefaults();
    void restrictedSearchAndDetailBehaviors_followFeatureSet();
};

void FeatureGateTest::cleanupTestCase()
{
    logging::Logger::instance().shutdown();
}

void FeatureGateTest::defaultState_isTrialAndPreviewOnly()
{
    const license::FeatureGate gate;

    QVERIFY(gate.isTrialMode());
    QVERIFY(!gate.isFullMode());
    assertFeatureMatrix(gate, true, false, false, false, false);
}

void FeatureGateTest::setLicenseState_fullState_syncsAndRespectsConfiguredFeatures()
{
    license::FeatureGate gate;

    const license::LicenseState full = buildFullState({
        QStringLiteral("basic_search_preview"),
        QStringLiteral("full_search"),
        QStringLiteral("unknown_feature"),
    });
    gate.setLicenseState(full);

    QVERIFY(gate.isFullMode());
    QVERIFY(!gate.isTrialMode());
    QCOMPARE(gate.licenseState().licenseSerial, QStringLiteral("LIC-2026-8888"));
    assertFeatureMatrix(gate, true, true, false, false, false);
}

void FeatureGateTest::setLicenseState_trialState_forcesTrialFeatureMatrix()
{
    license::FeatureGate gate;
    gate.setLicenseState(buildFullState({
        QStringLiteral("basic_search_preview"),
        QStringLiteral("full_search"),
        QStringLiteral("full_detail"),
        QStringLiteral("favorites"),
        QStringLiteral("advanced_filter"),
    }));
    QVERIFY(gate.isFullMode());

    gate.setLicenseState(buildTrialState(license::LicenseStatus::Invalid));

    QVERIFY(gate.isTrialMode());
    QVERIFY(!gate.isFullMode());
    assertFeatureMatrix(gate, true, false, false, false, false);
}

void FeatureGateTest::setLicenseState_fullStateWithoutConfiguredFeatures_fallsBackToFullDefaults()
{
    license::FeatureGate gate;

    gate.setLicenseState(buildFullState({}));

    QVERIFY(gate.isFullMode());
    assertFeatureMatrix(gate, true, true, true, true, true);
}

void FeatureGateTest::restrictedSearchAndDetailBehaviors_followFeatureSet()
{
    license::FeatureGate gate;
    gate.setLicenseState(buildFullState({
        QStringLiteral("basic_search_preview"),
        QStringLiteral("full_search"),
    }));

    QVERIFY(gate.isEnabled(license::Feature::FullSearch));
    QVERIFY(!gate.isEnabled(license::Feature::FullDetail));
    QVERIFY(!gate.isEnabled(license::Feature::Favorites));

    const QString detailReason = gate.disabledReason(license::Feature::FullDetail).trimmed();
    const QString favoritesReason = gate.disabledReason(license::Feature::Favorites).trimmed();
    QVERIFY(!detailReason.isEmpty());
    QVERIFY(!favoritesReason.isEmpty());

    // Current lightweight skeleton does not model an explicit export feature yet.
    QVERIFY(!license::FeatureGate::featureFromKey(QStringLiteral("export")).has_value());
}

QTEST_APPLESS_MAIN(FeatureGateTest)

#include "test_feature_gate.moc"

