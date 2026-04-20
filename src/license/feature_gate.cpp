#include "license/feature_gate.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"

namespace license {
namespace {

struct FeatureMeta final {
    Feature feature;
    const char* key;
    const char* shortCode;
    const char* trialDisabledReason;
};

constexpr FeatureMeta kFeatureMetas[] = {
    {Feature::BasicSearchPreview, "basic_search_preview", "bsp", "体验版已开放基础搜索预览。"},
    {Feature::FullSearch, "full_search", "fs", "体验版仅支持基础搜索预览，正式版解锁完整搜索。"},
    {Feature::FullDetail, "full_detail", "fd", "体验版仅支持部分详情预览，正式版解锁完整详情。"},
    {Feature::Favorites, "favorites", "fav", "体验版不支持收藏，正式版可收藏与管理结论。"},
    {Feature::AdvancedFilter, "advanced_filter", "af", "体验版不支持高级筛选，正式版解锁模块/分类/标签筛选。"},
};

const FeatureMeta* findFeatureMeta(Feature feature)
{
    for (const FeatureMeta& meta : kFeatureMetas) {
        if (meta.feature == feature) {
            return &meta;
        }
    }
    return nullptr;
}

}  // namespace

FeatureGate::FeatureGate()
{
    licenseState_.status = LicenseStatus::Trial;
    licenseState_.isTrial = true;
    licenseState_.isFull = false;
    setLicenseState(licenseState_);
}

bool FeatureGate::isEnabled(Feature feature) const
{
    return enabledFeatures_.contains(feature);
}

QString FeatureGate::disabledReason(Feature feature) const
{
    if (isEnabled(feature)) {
        return {};
    }

    if (isFullMode()) {
        return QStringLiteral("当前授权未包含该功能。");
    }

    const FeatureMeta* meta = findFeatureMeta(feature);
    if (meta != nullptr) {
        return QString::fromLatin1(meta->trialDisabledReason);
    }
    return QStringLiteral("该功能在当前授权下不可用。");
}

bool FeatureGate::isTrialMode() const
{
    return !isFullMode();
}

bool FeatureGate::isFullMode() const
{
    return licenseState_.isFull && licenseState_.status == LicenseStatus::ValidFull;
}

void FeatureGate::setLicenseState(const LicenseState& state)
{
    const bool oldFullMode = isFullMode();
    licenseState_ = state;
    enabledFeatures_.clear();

    if (isFullMode()) {
        QStringList configured = licenseState_.enabledFeatures;
        if (configured.isEmpty()) {
            configured = featureKeysFromList(fullFeatures());
        }

        for (const QString& featureKey : configured) {
            const std::optional<Feature> parsed = featureFromKey(featureKey);
            if (parsed.has_value()) {
                enabledFeatures_.insert(parsed.value());
            } else {
                LOG_WARN(LogCategory::Config,
                         QStringLiteral("feature gate ignored unknown feature_key=%1").arg(featureKey));
            }
        }

        if (enabledFeatures_.isEmpty()) {
            const QList<Feature> defaults = fullFeatures();
            for (const Feature feature : defaults) {
                enabledFeatures_.insert(feature);
            }
        }
    } else {
        const QList<Feature> defaults = trialFeatures();
        for (const Feature feature : defaults) {
            enabledFeatures_.insert(feature);
        }
    }

    if (oldFullMode != isFullMode()) {
        LOG_INFO(LogCategory::Config,
                 QStringLiteral("feature gate mode switched mode=%1 enabled=%2")
                     .arg(isFullMode() ? QStringLiteral("full") : QStringLiteral("trial"))
                     .arg(enabledFeatures_.size()));
    }
}

const LicenseState& FeatureGate::licenseState() const
{
    return licenseState_;
}

QString FeatureGate::featureToKey(Feature feature)
{
    const FeatureMeta* meta = findFeatureMeta(feature);
    return meta == nullptr ? QStringLiteral("unknown") : QString::fromLatin1(meta->key);
}

QString FeatureGate::featureToShortCode(Feature feature)
{
    const FeatureMeta* meta = findFeatureMeta(feature);
    return meta == nullptr ? QStringLiteral("unknown") : QString::fromLatin1(meta->shortCode);
}

std::optional<Feature> FeatureGate::featureFromKey(const QString& key)
{
    const QString normalized = key.trimmed().toLower();
    if (normalized.isEmpty()) {
        return std::nullopt;
    }

    for (const FeatureMeta& meta : kFeatureMetas) {
        if (normalized == QLatin1String(meta.key)) {
            return meta.feature;
        }
    }
    return std::nullopt;
}

std::optional<Feature> FeatureGate::featureFromShortCode(const QString& shortCode)
{
    const QString normalized = shortCode.trimmed().toLower();
    if (normalized.isEmpty()) {
        return std::nullopt;
    }

    for (const FeatureMeta& meta : kFeatureMetas) {
        if (normalized == QLatin1String(meta.shortCode)) {
            return meta.feature;
        }
    }
    return std::nullopt;
}

QList<Feature> FeatureGate::trialFeatures()
{
    return {Feature::BasicSearchPreview};
}

QList<Feature> FeatureGate::fullFeatures()
{
    return {
        Feature::BasicSearchPreview,
        Feature::FullSearch,
        Feature::FullDetail,
        Feature::Favorites,
        Feature::AdvancedFilter,
    };
}

QStringList FeatureGate::featureKeysFromList(const QList<Feature>& features)
{
    QStringList keys;
    keys.reserve(features.size());
    for (const Feature feature : features) {
        keys.push_back(featureToKey(feature));
    }
    return keys;
}

}  // namespace license

uint qHash(const license::Feature& feature, uint seed) noexcept
{
    return ::qHash(static_cast<int>(feature), seed);
}

