#pragma once

#include "domain/services/search_service.h"
#include "domain/services/suggest_service.h"
#include "infrastructure/data/conclusion_content_repository.h"
#include "infrastructure/data/conclusion_index_repository.h"
#include "license/activation_code_service.h"
#include "license/device_fingerprint_service.h"
#include "license/feature_gate.h"
#include "license/license_service.h"

#include <QString>
#include <QMainWindow>

class BottomStatusBar;
class ActivationPage;
class FavoritesPage;
class HomePage;
class NavigationSidebar;
class QStackedWidget;
class RecentSearchesPage;
class SearchPage;
class SettingsPage;
class TopBar;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    bool isIndexReady() const;
    bool isContentReady() const;
    bool isWebReady() const;
    int pageCount() const;

private slots:
    void switchPage(int pageIndex);

private:
    void switchPageWithTrigger(int pageIndex, const QString& trigger);
    void setupUi();
    void setupPages();
    void loadSearchData();
    void inspectRuntimeLayout();
    void updateBottomStatusBar() const;
    QString titleForPage(int pageIndex) const;
    QString subtitleForPage(int pageIndex) const;

    infrastructure::data::ConclusionIndexRepository indexRepository_;
    infrastructure::data::ConclusionContentRepository contentRepository_;
    domain::services::SearchService searchService_;
    domain::services::SuggestService suggestService_;
    license::DeviceFingerprintService deviceFingerprintService_;
    license::ActivationCodeService activationCodeService_;
    license::LicenseService licenseService_;
    license::FeatureGate featureGate_;

    bool indexLoaded_ = false;
    bool contentLoaded_ = false;
    bool webReady_ = false;
    bool runtimeLayoutHealthy_ = true;
    int currentPageIndex_ = -1;
    QString startupStatusLine_;
    QString runtimeStatusLine_;

    NavigationSidebar* navigationSidebar_ = nullptr;
    TopBar* topBar_ = nullptr;
    QStackedWidget* pageStack_ = nullptr;
    BottomStatusBar* bottomStatusBar_ = nullptr;
    HomePage* homePage_ = nullptr;
    SearchPage* searchPage_ = nullptr;
    FavoritesPage* favoritesPage_ = nullptr;
    RecentSearchesPage* recentSearchesPage_ = nullptr;
    SettingsPage* settingsPage_ = nullptr;
    ActivationPage* activationPage_ = nullptr;
};
