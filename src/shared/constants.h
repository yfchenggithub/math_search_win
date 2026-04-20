#pragma once

#include <QString>

namespace UiConstants {

constexpr int kDefaultWindowWidth = 1320;
constexpr int kDefaultWindowHeight = 860;
constexpr int kSidebarWidth = 220;
constexpr int kTopBarHeight = 88;
constexpr int kBottomBarHeight = 42;

constexpr int kPageHome = 0;
constexpr int kPageSearch = 1;
constexpr int kPageFavorites = 2;
constexpr int kPageRecentSearches = 3;
constexpr int kPageSettings = 4;
constexpr int kPageActivation = 5;

inline constexpr const char* pageName(int pageIndex)
{
    switch (pageIndex) {
    case kPageHome:
        return "home";
    case kPageSearch:
        return "search";
    case kPageFavorites:
        return "favorites";
    case kPageRecentSearches:
        return "recent_searches";
    case kPageSettings:
        return "settings";
    case kPageActivation:
        return "activation";
    default:
        return "unknown";
    }
}

inline const QString kAppTitle = QStringLiteral("高中数学二级结论搜索系统");
inline const QString kDefaultTopSubtitle = QStringLiteral("Windows 本地离线版 · MVP 第一轮工程骨架");
inline const QString kStatusOffline = QStringLiteral("本地离线模式");
inline const QString kStatusData = QStringLiteral("数据未加载");
inline const QString kStatusVersion = QStringLiteral("MVP v0.1");

}  // namespace UiConstants
