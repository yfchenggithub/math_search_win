#pragma once

#include <QDateTime>
#include <QString>

namespace ui::style {

namespace tokens {

inline constexpr int kPageOuterMargin = 24;
inline constexpr int kPageSectionSpacing = 22;
inline constexpr int kSmallSpacing = 8;
inline constexpr int kMediumSpacing = 12;
inline constexpr int kCardSpacing = 12;
inline constexpr int kCardPaddingHorizontal = 18;
inline constexpr int kCardPaddingVertical = 16;
inline constexpr int kEmptyCardPaddingHorizontal = 30;
inline constexpr int kEmptyCardPaddingVertical = 28;

inline constexpr int kCardRadius = 16;
inline constexpr int kButtonRadius = 11;
inline constexpr int kBadgeRadius = 9;

inline constexpr int kPageTitlePx = 22;
inline constexpr int kPageSubtitlePx = 13;
inline constexpr int kPageSummaryPx = 12;
inline constexpr int kCardTitlePx = 16;
inline constexpr int kCardBodyPx = 13;
inline constexpr int kCardMetaPx = 12;

}  // namespace tokens

bool ensureAppStyleSheetLoaded();
QString formatRelativeDateTime(const QDateTime& timestamp);
QString formatDifficultyText(double difficulty);

}  // namespace ui::style
