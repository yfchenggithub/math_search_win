#include "domain/models/app_settings.h"

namespace domain::models {

QVariantMap AppSettings::defaultValues()
{
    const auto key = [](const char* raw) { return QString::fromLatin1(raw); };

    return {
        {key(AppSettingKeys::AppVersion), QString()},
        {key(AppSettingKeys::DataVersion), QString()},
        {key(AppSettingKeys::WindowWidth), 1280},
        {key(AppSettingKeys::WindowHeight), 800},
        {key(AppSettingKeys::LastQuery), QString()},
        {key(AppSettingKeys::LastModuleFilter), QStringLiteral("all")},
        {key(AppSettingKeys::LastSortMode), QStringLiteral("relevance")},
        {key(AppSettingKeys::LastSelectedTags), QVariantList()},
        {key(AppSettingKeys::EditionHint), QStringLiteral("trial")},
        {key(AppSettingKeys::Theme), QStringLiteral("system")},
        {key(AppSettingKeys::DetailFontScaleLevel), 1},
    };
}

}  // namespace domain::models
