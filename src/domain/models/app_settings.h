#pragma once

#include <QVariantMap>

namespace domain::models {

namespace AppSettingKeys {
inline constexpr const char* AppVersion = "app_version";
inline constexpr const char* DataVersion = "data_version";
inline constexpr const char* WindowWidth = "window_width";
inline constexpr const char* WindowHeight = "window_height";
inline constexpr const char* LastQuery = "last_query";
inline constexpr const char* LastModuleFilter = "last_module_filter";
inline constexpr const char* LastSortMode = "last_sort_mode";
inline constexpr const char* LastSelectedTags = "last_selected_tags";
inline constexpr const char* EditionHint = "edition_hint";
inline constexpr const char* Theme = "theme";
inline constexpr const char* DetailFontScaleLevel = "detail_font_scale_level";
}  // namespace AppSettingKeys

struct AppSettings final {
    static constexpr int kVersion = 1;
    static QVariantMap defaultValues();
};

}  // namespace domain::models
