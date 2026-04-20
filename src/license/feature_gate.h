#pragma once

#include "license/license_state.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

namespace license {

enum class Feature {
    BasicSearchPreview,
    FullSearch,
    FullDetail,
    Favorites,
    AdvancedFilter
};

class FeatureGate final {
public:
    FeatureGate();

    bool isEnabled(Feature feature) const;
    QString disabledReason(Feature feature) const;
    bool isTrialMode() const;
    bool isFullMode() const;
    void setLicenseState(const LicenseState& state);
    const LicenseState& licenseState() const;

    static QString featureToKey(Feature feature);
    static QString featureToShortCode(Feature feature);
    static std::optional<Feature> featureFromKey(const QString& key);
    static std::optional<Feature> featureFromShortCode(const QString& shortCode);
    static QList<Feature> trialFeatures();
    static QList<Feature> fullFeatures();
    static QStringList featureKeysFromList(const QList<Feature>& features);

private:
    LicenseState licenseState_;
    QSet<Feature> enabledFeatures_;
};

}  // namespace license

uint qHash(const license::Feature& feature, uint seed = 0) noexcept;

