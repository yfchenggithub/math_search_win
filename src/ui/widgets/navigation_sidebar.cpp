#include "ui/widgets/navigation_sidebar.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"
#include "shared/constants.h"

#include <QButtonGroup>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

NavigationSidebar::NavigationSidebar(QWidget* parent)
    : QWidget(parent), buttonGroup_(new QButtonGroup(this))
{
    LOG_DEBUG(LogCategory::UiNavigation, QStringLiteral("NavigationSidebar constructor begin"));

    setFixedWidth(UiConstants::kSidebarWidth);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 16, 14, 16);
    layout->setSpacing(8);

    auto* appTitleLabel = new QLabel(UiConstants::kAppTitle, this);
    appTitleLabel->setWordWrap(true);
    layout->addWidget(appTitleLabel);

    auto* navHintLabel = new QLabel(QStringLiteral("页面导航"), this);
    layout->addWidget(navHintLabel);

    layout->addSpacing(8);

    addNavButton(layout, QStringLiteral("首页"), UiConstants::kPageHome);
    addNavButton(layout, QStringLiteral("搜索"), UiConstants::kPageSearch);
    addNavButton(layout, QStringLiteral("收藏"), UiConstants::kPageFavorites);
    addNavButton(layout, QStringLiteral("最近搜索"), UiConstants::kPageRecentSearches);
    addNavButton(layout, QStringLiteral("设置/关于"), UiConstants::kPageSettings);
    addNavButton(layout, QStringLiteral("激活/升级"), UiConstants::kPageActivation);

    layout->addStretch();

    connect(buttonGroup_, &QButtonGroup::idClicked, this, [this](int pageIndex) {
        LOG_INFO(LogCategory::UiNavigation, QStringLiteral("navigation clicked pageIndex=%1").arg(pageIndex));
        emit pageRequested(pageIndex);
    });

    LOG_DEBUG(LogCategory::UiNavigation, QStringLiteral("NavigationSidebar constructor complete"));
}

void NavigationSidebar::setCurrentIndex(int index)
{
    if (auto* button = buttonGroup_->button(index)) {
        button->setChecked(true);
        LOG_DEBUG(LogCategory::UiNavigation, QStringLiteral("setCurrentIndex index=%1 checked=true").arg(index));
        return;
    }

    LOG_WARN(LogCategory::UiNavigation, QStringLiteral("setCurrentIndex missing button index=%1").arg(index));
}

void NavigationSidebar::addNavButton(QVBoxLayout* layout, const QString& text, int pageIndex)
{
    auto* button = new QPushButton(text, this);
    button->setCheckable(true);
    button->setMinimumHeight(34);
    layout->addWidget(button);
    buttonGroup_->addButton(button, pageIndex);
    LOG_DEBUG(LogCategory::UiNavigation, QStringLiteral("addNavButton pageIndex=%1 text=%2").arg(pageIndex).arg(text));
}
