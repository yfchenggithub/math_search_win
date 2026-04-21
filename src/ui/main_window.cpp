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
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      searchService_(&indexRepository_),
      suggestService_(&indexRepository_),
      licenseService_(&deviceFingerprintService_)
{
    LOG_DEBUG(LogCategory::UiMainWindow, QStringLiteral("main_window ctor_begin"));

    resize(UiConstants::kDefaultWindowWidth, UiConstants::kDefaultWindowHeight);
    setWindowTitle(UiConstants::kAppTitle);

    licenseService_.initialize();
    featureGate_.setLicenseState(licenseService_.currentState());
    connect(&licenseService_, &license::LicenseService::licenseStateChanged, this, [this](const license::LicenseState& state) {
        featureGate_.setLicenseState(state);
        if (activationPage_ != nullptr) {
            activationPage_->reloadData();
        }
        if (settingsPage_ != nullptr) {
            settingsPage_->reloadData();
        }
        if (favoritesPage_ != nullptr) {
            favoritesPage_->reloadData();
        }
    });

    loadSearchData();
    setupUi();
    updateBottomStatusBar();

    if (searchPage_ != nullptr) {
        searchPage_->setBackendStatus(indexLoaded_, contentLoaded_);
        webReady_ = searchPage_->isDetailWebReady();
    }

    connect(navigationSidebar_, &NavigationSidebar::pageRequested, this, [this](int pageIndex) {
        switchPageWithTrigger(pageIndex, QStringLiteral("navigation"));
    });
    switchPageWithTrigger(UiConstants::kPageHome, QStringLiteral("startup_default"));

    const QString dataPath = AppPaths::dataDir();
    if (QDir(dataPath).exists()) {
        LOG_DEBUG(LogCategory::DataLoader, QStringLiteral("directory ready name=data path=%1").arg(dataPath));
    } else {
        LOG_WARN(LogCategory::DataLoader, QStringLiteral("data directory missing path=%1").arg(dataPath));
    }

    const QString cachePath = AppPaths::cacheDir();
    if (QDir(cachePath).exists()) {
        LOG_DEBUG(LogCategory::FileIo, QStringLiteral("directory ready name=cache path=%1").arg(cachePath));
    } else {
        LOG_WARN(LogCategory::FileIo, QStringLiteral("cache directory missing path=%1").arg(cachePath));
    }

    const QString licensePath = AppPaths::licenseDir();
    if (QDir(licensePath).exists()) {
        LOG_DEBUG(LogCategory::Config, QStringLiteral("directory ready name=license path=%1").arg(licensePath));
    } else {
        LOG_WARN(LogCategory::Config, QStringLiteral("license directory missing path=%1").arg(licensePath));
    }

    LOG_INFO(LogCategory::UiMainWindow,
             QStringLiteral("ui ready page_count=%1 web_ready=%2")
                 .arg(pageStack_ == nullptr ? 0 : pageStack_->count())
                 .arg(webReady_ ? QStringLiteral("true") : QStringLiteral("false")));
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

    homePage_ = new HomePage(&indexRepository_, pageStack_);
    pageStack_->addWidget(homePage_);
    LOG_DEBUG(LogCategory::UiMainWindow, QStringLiteral("registered page index=%1 name=home").arg(UiConstants::kPageHome));

    searchPage_ = new SearchPage(&searchService_,
                                 &suggestService_,
                                 &contentRepository_,
                                 &indexRepository_,
                                 &featureGate_,
                                 &licenseService_,
                                 pageStack_);
    pageStack_->addWidget(searchPage_);
    LOG_DEBUG(LogCategory::UiMainWindow, QStringLiteral("registered page index=%1 name=search").arg(UiConstants::kPageSearch));

    favoritesPage_ = new FavoritesPage(&contentRepository_, &indexRepository_, &featureGate_, &licenseService_, pageStack_);
    pageStack_->addWidget(favoritesPage_);
    LOG_DEBUG(LogCategory::UiMainWindow,
              QStringLiteral("registered page index=%1 name=favorites").arg(UiConstants::kPageFavorites));
    recentSearchesPage_ = new RecentSearchesPage(pageStack_);
    pageStack_->addWidget(recentSearchesPage_);
    LOG_DEBUG(LogCategory::UiMainWindow,
              QStringLiteral("registered page index=%1 name=recent_searches").arg(UiConstants::kPageRecentSearches));
    settingsPage_ =
        new SettingsPage(&indexRepository_, &contentRepository_, &licenseService_, indexLoaded_, contentLoaded_, pageStack_);
    pageStack_->addWidget(settingsPage_);
    LOG_DEBUG(LogCategory::UiMainWindow,
              QStringLiteral("registered page index=%1 name=settings").arg(UiConstants::kPageSettings));
    activationPage_ = new ActivationPage(&licenseService_, &deviceFingerprintService_, &activationCodeService_, pageStack_);
    pageStack_->addWidget(activationPage_);
    LOG_DEBUG(LogCategory::UiMainWindow,
              QStringLiteral("registered page index=%1 name=activation").arg(UiConstants::kPageActivation));

    if (recentSearchesPage_ != nullptr) {
        connect(recentSearchesPage_, &RecentSearchesPage::searchRequested, this, [this](const QString& query) {
            if (searchPage_ == nullptr) {
                return;
            }

            switchPageWithTrigger(UiConstants::kPageSearch, QStringLiteral("recent_search_item"));
            searchPage_->triggerSearchFromRecent(query);
        });

        connect(recentSearchesPage_, &RecentSearchesPage::navigateToSearchRequested, this, [this]() {
            switchPageWithTrigger(UiConstants::kPageSearch, QStringLiteral("recent_empty_action"));
        });

        connect(recentSearchesPage_, &RecentSearchesPage::historyChanged, this, [this]() {
            if (homePage_ != nullptr) {
                homePage_->reloadData();
            }
        });
    }

    if (favoritesPage_ != nullptr) {
        connect(favoritesPage_, &FavoritesPage::openConclusionRequested, this, [this](const QString& conclusionId) {
            if (searchPage_ == nullptr) {
                return;
            }

            switchPageWithTrigger(UiConstants::kPageSearch, QStringLiteral("favorites_open_detail"));
            searchPage_->openConclusionById(conclusionId);
        });

        connect(favoritesPage_, &FavoritesPage::navigateToSearchRequested, this, [this]() {
            switchPageWithTrigger(UiConstants::kPageSearch, QStringLiteral("favorites_empty_action"));
        });

        connect(favoritesPage_, &FavoritesPage::favoritesChanged, this, [this]() {
            if (searchPage_ != nullptr) {
                searchPage_->refreshFavoriteState();
            }
            if (homePage_ != nullptr) {
                homePage_->reloadData();
            }
        });
    }

    if (searchPage_ != nullptr) {
        connect(searchPage_, &SearchPage::favoritesChanged, this, [this]() {
            if (favoritesPage_ != nullptr) {
                favoritesPage_->reloadData();
            }
            if (homePage_ != nullptr) {
                homePage_->reloadData();
            }
        });

        connect(searchPage_, &SearchPage::historyChanged, this, [this]() {
            if (homePage_ != nullptr) {
                homePage_->reloadData();
            }
            if (recentSearchesPage_ != nullptr) {
                recentSearchesPage_->reloadData();
            }
        });
    }

    if (homePage_ != nullptr) {
        connect(homePage_, &HomePage::navigateToSearchRequested, this, [this]() {
            switchPageWithTrigger(UiConstants::kPageSearch, QStringLiteral("home_primary_search"));
        });
        connect(homePage_, &HomePage::navigateToRecentRequested, this, [this]() {
            switchPageWithTrigger(UiConstants::kPageRecentSearches, QStringLiteral("home_recent_entry"));
        });
        connect(homePage_, &HomePage::navigateToFavoritesRequested, this, [this]() {
            switchPageWithTrigger(UiConstants::kPageFavorites, QStringLiteral("home_favorites_entry"));
        });
        connect(homePage_, &HomePage::navigateToSettingsRequested, this, [this]() {
            switchPageWithTrigger(UiConstants::kPageSettings, QStringLiteral("home_settings_entry"));
        });
        connect(homePage_, &HomePage::navigateToActivationRequested, this, [this]() {
            switchPageWithTrigger(UiConstants::kPageActivation, QStringLiteral("home_activation_entry"));
        });
        connect(homePage_, &HomePage::searchRequested, this, [this](const QString& query) {
            if (searchPage_ == nullptr) {
                return;
            }
            switchPageWithTrigger(UiConstants::kPageSearch, QStringLiteral("home_recent_preview_search"));
            searchPage_->triggerSearchFromRecent(query);
        });
        connect(homePage_, &HomePage::openConclusionRequested, this, [this](const QString& conclusionId) {
            if (searchPage_ == nullptr) {
                return;
            }
            switchPageWithTrigger(UiConstants::kPageSearch, QStringLiteral("home_favorites_preview_open"));
            searchPage_->openConclusionById(conclusionId);
        });
    }

    LOG_DEBUG(LogCategory::UiMainWindow, QStringLiteral("setup_pages done page_count=%1").arg(pageStack_->count()));
}

void MainWindow::loadSearchData()
{
    indexLoaded_ = indexRepository_.loadFromFile();
    contentLoaded_ = contentRepository_.loadFromFile();

    if (indexLoaded_) {
        LOG_DEBUG(LogCategory::SearchIndex,
                  QStringLiteral("index loaded docs=%1 terms=%2 prefixes=%3 modules=%4 path=%5")
                      .arg(indexRepository_.docCount())
                      .arg(indexRepository_.termCount())
                      .arg(indexRepository_.prefixCount())
                      .arg(indexRepository_.modules().size())
                      .arg(indexRepository_.activeIndexPath()));
    } else {
        const auto& diagnostics = indexRepository_.diagnostics();
        LOG_ERROR(LogCategory::SearchIndex,
                  QStringLiteral("index load failed fatal=%1 warnings=%2")
                      .arg(diagnostics.fatalError, QString::number(diagnostics.warnings.size())));
    }

    if (contentLoaded_) {
        LOG_DEBUG(LogCategory::DataLoader,
                  QStringLiteral("content loaded records=%1 modules=%2 tags=%3 path=%4")
                      .arg(contentRepository_.size())
                      .arg(contentRepository_.modules().size())
                      .arg(contentRepository_.tags().size())
                      .arg(contentRepository_.activeContentPath()));
    } else {
        const auto& diagnostics = contentRepository_.diagnostics();
        LOG_ERROR(LogCategory::DataLoader,
                  QStringLiteral("content load failed fatal=%1 warnings=%2 skipped=%3")
                      .arg(diagnostics.fatalError)
                      .arg(diagnostics.warnings.size())
                      .arg(diagnostics.skippedCount));
    }

    LOG_INFO(LogCategory::DataLoader,
             QStringLiteral("data loaded index_ready=%1 content_ready=%2 index_docs=%3 content_records=%4 modules=%5")
                 .arg(indexLoaded_ ? QStringLiteral("true") : QStringLiteral("false"))
                 .arg(contentLoaded_ ? QStringLiteral("true") : QStringLiteral("false"))
                 .arg(indexLoaded_ ? QString::number(indexRepository_.docCount()) : QStringLiteral("0"))
                 .arg(contentLoaded_ ? QString::number(contentRepository_.size()) : QStringLiteral("0"))
                 .arg(indexLoaded_ ? QString::number(indexRepository_.modules().size()) : QStringLiteral("0")));

    if (indexLoaded_ && contentLoaded_) {
        startupStatusLine_ =
            QStringLiteral("数据已加载 content=%1 index=%2 modules=%3")
                .arg(contentRepository_.size())
                .arg(indexRepository_.docCount())
                .arg(indexRepository_.modules().size());
        return;
    }

    QStringList errorParts;
    if (!indexLoaded_) {
        const QString indexReason = indexRepository_.diagnostics().fatalError.trimmed();
        errorParts.push_back(indexReason.isEmpty() ? QStringLiteral("索引加载失败")
                                                   : QStringLiteral("索引失败: %1").arg(indexReason));
    }
    if (!contentLoaded_) {
        const QString contentReason = contentRepository_.diagnostics().fatalError.trimmed();
        errorParts.push_back(contentReason.isEmpty() ? QStringLiteral("内容加载失败")
                                                     : QStringLiteral("内容失败: %1").arg(contentReason));
    }
    startupStatusLine_ = errorParts.join(QStringLiteral(" | "));
}

void MainWindow::updateBottomStatusBar() const
{
    if (bottomStatusBar_ == nullptr) {
        return;
    }

    bottomStatusBar_->setDataStatusText(startupStatusLine_);
    if (indexLoaded_ && contentLoaded_) {
        bottomStatusBar_->setVersionStatusText(QStringLiteral("MVP v0.1 · 数据就绪"));
    } else {
        bottomStatusBar_->setVersionStatusText(QStringLiteral("MVP v0.1 · 数据异常"));
    }
}

void MainWindow::switchPage(int pageIndex)
{
    switchPageWithTrigger(pageIndex, QStringLiteral("navigation"));
}

void MainWindow::switchPageWithTrigger(int pageIndex, const QString& trigger)
{
    if (!pageStack_ || pageIndex < 0 || pageIndex >= pageStack_->count()) {
        const int pageCount = pageStack_ ? pageStack_->count() : 0;
        LOG_WARN(LogCategory::UiMainWindow,
                 QStringLiteral("page switch ignored to=%1 trigger=%2 page_count=%3 page_stack_null=%4")
                     .arg(QString::fromUtf8(UiConstants::pageName(pageIndex)))
                     .arg(trigger.trimmed().isEmpty() ? QStringLiteral("unknown") : trigger.trimmed())
                     .arg(pageCount)
                     .arg(pageStack_ == nullptr ? QStringLiteral("true") : QStringLiteral("false")));
        return;
    }

    const QString fromPage = QString::fromUtf8(UiConstants::pageName(currentPageIndex_));
    const QString toPage = QString::fromUtf8(UiConstants::pageName(pageIndex));

    pageStack_->setCurrentIndex(pageIndex);
    navigationSidebar_->setCurrentIndex(pageIndex);
    topBar_->setPageTitle(titleForPage(pageIndex), subtitleForPage(pageIndex));
    currentPageIndex_ = pageIndex;

    if (pageIndex == UiConstants::kPageRecentSearches && recentSearchesPage_ != nullptr) {
        recentSearchesPage_->reloadData();
    }
    if (pageIndex == UiConstants::kPageSearch && searchPage_ != nullptr) {
        searchPage_->refreshFavoriteState();
    }
    if (pageIndex == UiConstants::kPageFavorites && favoritesPage_ != nullptr) {
        favoritesPage_->reloadData();
    }
    if (pageIndex == UiConstants::kPageHome && homePage_ != nullptr) {
        homePage_->reloadData();
    }
    if (pageIndex == UiConstants::kPageSettings && settingsPage_ != nullptr) {
        settingsPage_->reloadData();
    }
    if (pageIndex == UiConstants::kPageActivation && activationPage_ != nullptr) {
        activationPage_->reloadData();
    }

    const QString triggerText = trigger.trimmed().isEmpty() ? QStringLiteral("unknown") : trigger.trimmed();
    const QString message = QStringLiteral("page switched from=%1 to=%2 trigger=%3")
                                .arg(fromPage.isEmpty() ? QStringLiteral("unknown") : fromPage, toPage, triggerText);
    if (triggerText == QStringLiteral("startup_default")) {
        LOG_DEBUG(LogCategory::UiMainWindow, message);
    } else {
        LOG_INFO(LogCategory::UiMainWindow, message);
    }
}

bool MainWindow::isIndexReady() const
{
    return indexLoaded_;
}

bool MainWindow::isContentReady() const
{
    return contentLoaded_;
}

bool MainWindow::isWebReady() const
{
    return webReady_;
}

int MainWindow::pageCount() const
{
    return pageStack_ == nullptr ? 0 : pageStack_->count();
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
        return QStringLiteral("主路径入口：先搜索，再回访最近和收藏");
    case UiConstants::kPageSearch:
        return QStringLiteral("本页已接入本地检索与 WebEngine 详情渲染");
    case UiConstants::kPageFavorites:
        return QStringLiteral("收藏的二级结论，便于回看与复习");
    case UiConstants::kPageRecentSearches:
        return QStringLiteral("快速回访近期检索内容");
    case UiConstants::kPageSettings:
        return QStringLiteral("管理应用信息、数据状态与帮助入口");
    case UiConstants::kPageActivation:
        return QStringLiteral("状态确认、激活操作与升级说明");
    default:
        return UiConstants::kDefaultTopSubtitle;
    }
}
