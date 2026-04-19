#include "ui/main_window.h"

#include "shared/constants.h"
#include "ui/pages/activation_page.h"
#include "ui/pages/favorites_page.h"
#include "ui/pages/home_page.h"
#include "ui/pages/recent_searches_page.h"
#include "ui/pages/search_page.h"
#include "ui/pages/settings_page.h"
#include "ui/widgets/bottom_status_bar.h"
#include "ui/widgets/navigation_sidebar.h"
#include "ui/widgets/top_bar.h"

#include <QHBoxLayout>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    resize(UiConstants::kDefaultWindowWidth, UiConstants::kDefaultWindowHeight);
    setWindowTitle(UiConstants::kAppTitle);

    setupUi();

    connect(navigationSidebar_, &NavigationSidebar::pageRequested, this, &MainWindow::switchPage);
    switchPage(UiConstants::kPageHome);
}

void MainWindow::setupUi()
{
    auto* central = new QWidget(this);
    auto* rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    navigationSidebar_ = new NavigationSidebar(central);
    rootLayout->addWidget(navigationSidebar_);

    auto* rightPanel = new QWidget(central);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    topBar_ = new TopBar(rightPanel);
    pageStack_ = new QStackedWidget(rightPanel);
    bottomStatusBar_ = new BottomStatusBar(rightPanel);

    setupPages();

    rightLayout->addWidget(topBar_);
    rightLayout->addWidget(pageStack_, 1);
    rightLayout->addWidget(bottomStatusBar_);

    rootLayout->addWidget(rightPanel, 1);
    setCentralWidget(central);
}

void MainWindow::setupPages()
{
    pageStack_->addWidget(new HomePage(pageStack_));
    pageStack_->addWidget(new SearchPage(pageStack_));
    pageStack_->addWidget(new FavoritesPage(pageStack_));
    pageStack_->addWidget(new RecentSearchesPage(pageStack_));
    pageStack_->addWidget(new SettingsPage(pageStack_));
    pageStack_->addWidget(new ActivationPage(pageStack_));
}

void MainWindow::switchPage(int pageIndex)
{
    if (!pageStack_ || pageIndex < 0 || pageIndex >= pageStack_->count()) {
        return;
    }

    pageStack_->setCurrentIndex(pageIndex);
    navigationSidebar_->setCurrentIndex(pageIndex);
    topBar_->setPageTitle(titleForPage(pageIndex), subtitleForPage(pageIndex));
}

QString MainWindow::titleForPage(int pageIndex) const
{
    switch (pageIndex) {
    case UiConstants::kPageHome:
        return QStringLiteral("首页");
    case UiConstants::kPageSearch:
        return QStringLiteral("搜索");
    case UiConstants::kPageFavorites:
        return QStringLiteral("收藏");
    case UiConstants::kPageRecentSearches:
        return QStringLiteral("最近搜索");
    case UiConstants::kPageSettings:
        return QStringLiteral("设置/关于");
    case UiConstants::kPageActivation:
        return QStringLiteral("激活/升级");
    default:
        return QStringLiteral("页面");
    }
}

QString MainWindow::subtitleForPage(int pageIndex) const
{
    switch (pageIndex) {
    case UiConstants::kPageHome:
        return QStringLiteral("欢迎使用本地离线版骨架");
    case UiConstants::kPageSearch:
        return QStringLiteral("搜索流程静态占位，下一轮接入真实检索");
    case UiConstants::kPageFavorites:
        return QStringLiteral("收藏列表静态占位");
    case UiConstants::kPageRecentSearches:
        return QStringLiteral("最近搜索静态占位");
    case UiConstants::kPageSettings:
        return QStringLiteral("设置与帮助信息占位");
    case UiConstants::kPageActivation:
        return QStringLiteral("激活与升级入口占位");
    default:
        return UiConstants::kDefaultTopSubtitle;
    }
}

