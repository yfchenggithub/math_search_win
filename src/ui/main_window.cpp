#include "ui/main_window.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"
#include "shared/constants.h"
#include "shared/paths.h"
#include "ui/pages/activation_page.h"
#include "ui/pages/favorites_page.h"
#include "ui/pages/home_page.h"
#include "ui/pages/recent_searches_page.h"
#include "ui/pages/search_page.h"
#include "ui/pages/settings_page.h"
#include "ui/widgets/bottom_status_bar.h"
#include "ui/widgets/navigation_sidebar.h"
#include "ui/widgets/top_bar.h"

#include <QDir>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    LOG_INFO(LogCategory::UiMainWindow, QStringLiteral("MainWindow constructor begin"));

    resize(UiConstants::kDefaultWindowWidth, UiConstants::kDefaultWindowHeight);
    setWindowTitle(UiConstants::kAppTitle);

    setupUi();

    connect(navigationSidebar_, &NavigationSidebar::pageRequested, this, &MainWindow::switchPage);
    switchPage(UiConstants::kPageHome);

    const QString dataPath = AppPaths::dataDir();
    if (QDir(dataPath).exists()) {
        LOG_INFO(LogCategory::DataLoader, QStringLiteral("data directory ready path=%1").arg(dataPath));
    } else {
        LOG_WARN(LogCategory::DataLoader, QStringLiteral("data directory missing path=%1").arg(dataPath));
    }

    const QString cachePath = AppPaths::cacheDir();
    if (QDir(cachePath).exists()) {
        LOG_INFO(LogCategory::FileIo, QStringLiteral("cache directory ready path=%1").arg(cachePath));
    } else {
        LOG_WARN(LogCategory::FileIo, QStringLiteral("cache directory missing path=%1").arg(cachePath));
    }

    const QString licensePath = AppPaths::licenseDir();
    if (QDir(licensePath).exists()) {
        LOG_INFO(LogCategory::Config, QStringLiteral("license directory ready path=%1").arg(licensePath));
    } else {
        LOG_WARN(LogCategory::Config, QStringLiteral("license directory missing path=%1").arg(licensePath));
    }

    LOG_INFO(LogCategory::UiMainWindow, QStringLiteral("MainWindow constructor complete"));
}

void MainWindow::setupUi()
{
    LOG_DEBUG(LogCategory::UiMainWindow, QStringLiteral("setupUi begin"));

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

    LOG_DEBUG(LogCategory::UiMainWindow, QStringLiteral("setupUi complete"));
}

void MainWindow::setupPages()
{
    LOG_DEBUG(LogCategory::UiMainWindow, QStringLiteral("setupPages begin"));

    pageStack_->addWidget(new HomePage(pageStack_));
    LOG_DEBUG(LogCategory::UiMainWindow, QStringLiteral("registered page index=%1 name=home").arg(UiConstants::kPageHome));
    pageStack_->addWidget(new SearchPage(pageStack_));
    LOG_DEBUG(LogCategory::UiMainWindow, QStringLiteral("registered page index=%1 name=search").arg(UiConstants::kPageSearch));
    pageStack_->addWidget(new FavoritesPage(pageStack_));
    LOG_DEBUG(LogCategory::UiMainWindow,
              QStringLiteral("registered page index=%1 name=favorites").arg(UiConstants::kPageFavorites));
    pageStack_->addWidget(new RecentSearchesPage(pageStack_));
    LOG_DEBUG(LogCategory::UiMainWindow,
              QStringLiteral("registered page index=%1 name=recent_searches").arg(UiConstants::kPageRecentSearches));
    pageStack_->addWidget(new SettingsPage(pageStack_));
    LOG_DEBUG(LogCategory::UiMainWindow,
              QStringLiteral("registered page index=%1 name=settings").arg(UiConstants::kPageSettings));
    pageStack_->addWidget(new ActivationPage(pageStack_));
    LOG_DEBUG(LogCategory::UiMainWindow,
              QStringLiteral("registered page index=%1 name=activation").arg(UiConstants::kPageActivation));

    LOG_INFO(LogCategory::UiMainWindow, QStringLiteral("setupPages complete pageCount=%1").arg(pageStack_->count()));
}

void MainWindow::switchPage(int pageIndex)
{
    if (!pageStack_ || pageIndex < 0 || pageIndex >= pageStack_->count()) {
        const int pageCount = pageStack_ ? pageStack_->count() : 0;
        LOG_WARN(LogCategory::UiMainWindow,
                 QStringLiteral("switchPage ignored invalid pageIndex=%1 pageCount=%2 pageStackNull=%3")
                     .arg(pageIndex)
                     .arg(pageCount)
                     .arg(pageStack_ == nullptr ? QStringLiteral("true") : QStringLiteral("false")));
        return;
    }

    pageStack_->setCurrentIndex(pageIndex);
    navigationSidebar_->setCurrentIndex(pageIndex);
    topBar_->setPageTitle(titleForPage(pageIndex), subtitleForPage(pageIndex));
    LOG_INFO(LogCategory::UiMainWindow, QStringLiteral("switchPage success pageIndex=%1").arg(pageIndex));
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
