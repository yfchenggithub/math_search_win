#include "core/logging/logger.h"
#include "domain/adapters/conclusion_detail_adapter.h"
#include "domain/repositories/favorites_repository.h"
#include "domain/repositories/history_repository.h"
#include "license/device_fingerprint_service.h"
#include "license/feature_gate.h"
#include "shared/constants.h"
#include "ui/pages/activation_page.h"
#include "ui/pages/favorites_page.h"
#include "ui/pages/home_page.h"
#include "ui/pages/recent_searches_page.h"
#include "ui/widgets/navigation_sidebar.h"
#include "ui/widgets/favorites/favorite_item_card.h"
#include "ui/widgets/recent_search_item_widget.h"

#define private public
#include "ui/main_window.h"
#include "ui/pages/search_page.h"
#include "ui/pages/settings_page.h"
#undef private

#include <QtTest/QtTest>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QStackedWidget>

#ifndef MATH_SEARCH_TESTS_SOURCE_DIR
#error "MATH_SEARCH_TESTS_SOURCE_DIR is not defined for tests"
#endif

namespace {

QString testsSourceDir()
{
    return QDir(QString::fromUtf8(MATH_SEARCH_TESTS_SOURCE_DIR)).absolutePath();
}

bool copyFileStrict(const QString& sourcePath, const QString& targetPath)
{
    if (!QFileInfo::exists(sourcePath)) {
        return false;
    }

    const QFileInfo targetInfo(targetPath);
    QDir targetDir = targetInfo.absoluteDir();
    if (!targetDir.exists() && !QDir().mkpath(targetDir.absolutePath())) {
        return false;
    }

    if (QFileInfo::exists(targetPath) && !QFile::remove(targetPath)) {
        return false;
    }

    return QFile::copy(sourcePath, targetPath);
}

QJsonObject makeCanonicalRecord(const QString& id,
                                const QString& module,
                                const QString& title,
                                const QString& category,
                                const QStringList& tags)
{
    QJsonObject identity;
    identity.insert(QStringLiteral("slug"), id.toLower());
    identity.insert(QStringLiteral("module"), module);
    identity.insert(QStringLiteral("knowledge_node"), QStringLiteral("round5-node"));

    QJsonObject meta;
    meta.insert(QStringLiteral("title"), title);
    meta.insert(QStringLiteral("difficulty"), 2);
    meta.insert(QStringLiteral("category"), category);
    QJsonArray tagsArray;
    for (const QString& tag : tags) {
        tagsArray.push_back(tag);
    }
    meta.insert(QStringLiteral("tags"), tagsArray);
    meta.insert(QStringLiteral("summary"), QStringLiteral("round5 summary for %1").arg(id));
    meta.insert(QStringLiteral("is_pro"), false);

    QJsonObject plain;
    plain.insert(QStringLiteral("statement"), QStringLiteral("statement %1").arg(id));
    plain.insert(QStringLiteral("summary"), QStringLiteral("summary %1").arg(id));

    QJsonObject content;
    content.insert(QStringLiteral("render_schema_version"), 2);
    content.insert(QStringLiteral("primary_formula"), QStringLiteral("x+y>=2sqrt(xy)"));
    content.insert(QStringLiteral("plain"), plain);

    QJsonObject record;
    record.insert(QStringLiteral("id"), id);
    record.insert(QStringLiteral("schema_version"), 2);
    record.insert(QStringLiteral("type"), QStringLiteral("conclusion"));
    record.insert(QStringLiteral("status"), QStringLiteral("published"));
    record.insert(QStringLiteral("identity"), identity);
    record.insert(QStringLiteral("meta"), meta);
    record.insert(QStringLiteral("content"), content);
    return record;
}

class ScopedSandboxRoot final {
public:
    ScopedSandboxRoot()
        : previousCwd_(QDir::currentPath())
    {
        const QString baseDir = QDir(previousCwd_).filePath(QStringLiteral(".tmp_main_window_round5_tests"));
        if (!QDir().mkpath(baseDir)) {
            return;
        }

        rootPath_ = QDir(baseDir).filePath(
            QStringLiteral("case_%1_%2")
                .arg(QDateTime::currentMSecsSinceEpoch())
                .arg(QRandomGenerator::global()->bounded(1000000)));
        if (!QDir().mkpath(rootPath_)) {
            rootPath_.clear();
            return;
        }

        QDir root(rootPath_);
        const QStringList requiredDirs = {
            QStringLiteral("src"),
            QStringLiteral("resources"),
            QStringLiteral("data"),
            QStringLiteral("cache"),
            QStringLiteral("license"),
        };
        for (const QString& dir : requiredDirs) {
            if (!root.mkpath(dir)) {
                rootPath_.clear();
                return;
            }
        }

        QDir::setCurrent(rootPath_);
    }

    ~ScopedSandboxRoot()
    {
        if (!previousCwd_.isEmpty()) {
            QDir::setCurrent(previousCwd_);
        }
        if (!rootPath_.isEmpty()) {
            QDir(rootPath_).removeRecursively();
        }
    }

    bool isValid() const
    {
        return !rootPath_.isEmpty()
            && QDir(rootPath_).exists(QStringLiteral("src"))
            && QDir(rootPath_).exists(QStringLiteral("resources"))
            && QDir(rootPath_).exists(QStringLiteral("data"))
            && QDir(rootPath_).exists(QStringLiteral("cache"))
            && QDir(rootPath_).exists(QStringLiteral("license"));
    }

    QString path(const QString& relativePath) const
    {
        return QDir(rootPath_).filePath(relativePath);
    }

    bool installRound2IndexFixture() const
    {
        const QString sourcePath =
            QDir(testsSourceDir()).filePath(QStringLiteral("fixtures/test_backend_search_index_round2.json"));
        return copyFileStrict(sourcePath, path(QStringLiteral("data/backend_search_index.json")));
    }

    bool writeCanonicalContentFixture() const
    {
        QJsonObject root;
        root.insert(QStringLiteral("X001"),
                    makeCanonicalRecord(QStringLiteral("X001"),
                                        QStringLiteral("algebra"),
                                        QStringLiteral("Exact Match Core"),
                                        QStringLiteral("inequality"),
                                        {QStringLiteral("core"), QStringLiteral("alpha")}));
        root.insert(QStringLiteral("X002"),
                    makeCanonicalRecord(QStringLiteral("X002"),
                                        QStringLiteral("geometry"),
                                        QStringLiteral("Prefix Geometry"),
                                        QStringLiteral("triangle"),
                                        {QStringLiteral("geo"), QStringLiteral("alpha")}));

        QFile file(path(QStringLiteral("data/canonical_content_v2.json")));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            return false;
        }
        const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
        const qint64 written = file.write(payload);
        file.close();
        return written == payload.size();
    }

    bool writeFullLicenseForCurrentDevice() const
    {
        const license::DeviceFingerprintService fingerprintService;
        const QString deviceFingerprint = fingerprintService.deviceFingerprint().trimmed();
        if (deviceFingerprint.isEmpty()) {
            return false;
        }

        const QStringList featureKeys =
            license::FeatureGate::featureKeysFromList(license::FeatureGate::fullFeatures());
        const QString lines = QStringList{
                                  QStringLiteral("format=msw-license-v1"),
                                  QStringLiteral("product=math_search_win"),
                                  QStringLiteral("serial=LIC-ROUND5-0001"),
                                  QStringLiteral("watermark=WM-ROUND5-0001"),
                                  QStringLiteral("edition=full"),
                                  QStringLiteral("device=%1").arg(deviceFingerprint),
                                  QStringLiteral("features=%1").arg(featureKeys.join(QLatin1Char(','))),
                                  QStringLiteral("issued_at=2026-04-20"),
                                  QStringLiteral("expire_at=2099-12-31"),
                                  QStringLiteral("issuer=round5-ui-tests"),
                                  QStringLiteral("source=round5-ui-tests"),
                                  QStringLiteral("status=valid"),
                              }
                                  .join(QLatin1Char('\n'))
                              + QLatin1Char('\n');

        QFile file(path(QStringLiteral("license/license.dat")));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            return false;
        }
        const QByteArray payload = lines.toUtf8();
        const qint64 written = file.write(payload);
        file.close();
        return written == payload.size();
    }

    bool seedFavorite(const QString& conclusionId) const
    {
        domain::repositories::FavoritesRepository repository;
        if (!repository.load()) {
            return false;
        }
        repository.clear();
        repository.add(conclusionId);
        if (!repository.load()) {
            return false;
        }
        return repository.contains(conclusionId);
    }

    bool seedRecentQuery(const QString& queryText) const
    {
        domain::repositories::HistoryRepository repository;
        if (!repository.load()) {
            return false;
        }
        repository.clear();
        repository.addQuery(queryText, QStringLiteral("manual"));
        if (!repository.load()) {
            return false;
        }
        return repository.count() == 1;
    }

private:
    QString previousCwd_;
    QString rootPath_;
};

bool hasHomePreviewTitle(const HomePage* homePage, const QString& expectedTitle)
{
    if (homePage == nullptr) {
        return false;
    }

    const QList<QLabel*> titleLabels = homePage->findChildren<QLabel*>(QStringLiteral("homePreviewItemTitle"));
    for (QLabel* label : titleLabels) {
        if (label == nullptr) {
            continue;
        }
        if (qobject_cast<QPushButton*>(label->parentWidget()) == nullptr) {
            continue;
        }
        if (label->text().trimmed() == expectedTitle) {
            return true;
        }
    }
    return false;
}

}  // namespace

class MainWindowRound5Test final : public QObject {
    Q_OBJECT

private slots:
    void cleanupTestCase();

    void mainWindow_setupPages_createsAllPagesAndStableIndices();
    void mainWindow_loadSearchData_beforePageDependenciesUse();
    void mainWindow_switchPageWithTrigger_movesMainStack();
    void crossPage_favoritesChanged_refreshesFavoritesAndHome();
    void crossPage_recentClick_jumpsBackToSearch();
    void crossPage_favoritesOpenEntry_navigatesToSearchDetail();
};

void MainWindowRound5Test::cleanupTestCase()
{
    logging::Logger::instance().shutdown();
}

void MainWindowRound5Test::mainWindow_setupPages_createsAllPagesAndStableIndices()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");
    QVERIFY2(sandbox.installRound2IndexFixture(), "round2 index fixture should be copied into sandbox");
    QVERIFY2(sandbox.writeCanonicalContentFixture(), "canonical content fixture should be written");

    MainWindow window(nullptr);

    QVERIFY(window.navigationSidebar_ != nullptr);
    QVERIFY(window.topBar_ != nullptr);
    QVERIFY(window.bottomStatusBar_ != nullptr);
    QVERIFY(window.pageStack_ != nullptr);
    QVERIFY(window.homePage_ != nullptr);
    QVERIFY(window.searchPage_ != nullptr);
    QVERIFY(window.favoritesPage_ != nullptr);
    QVERIFY(window.recentSearchesPage_ != nullptr);
    QVERIFY(window.settingsPage_ != nullptr);
    QVERIFY(window.activationPage_ != nullptr);

    QCOMPARE(window.pageCount(), 6);
    QCOMPARE(window.pageStack_->count(), 6);

    QCOMPARE(window.pageStack_->widget(UiConstants::kPageHome), static_cast<QWidget*>(window.homePage_));
    QCOMPARE(window.pageStack_->widget(UiConstants::kPageSearch), static_cast<QWidget*>(window.searchPage_));
    QCOMPARE(window.pageStack_->widget(UiConstants::kPageFavorites), static_cast<QWidget*>(window.favoritesPage_));
    QCOMPARE(window.pageStack_->widget(UiConstants::kPageRecentSearches),
             static_cast<QWidget*>(window.recentSearchesPage_));
    QCOMPARE(window.pageStack_->widget(UiConstants::kPageSettings), static_cast<QWidget*>(window.settingsPage_));
    QCOMPARE(window.pageStack_->widget(UiConstants::kPageActivation), static_cast<QWidget*>(window.activationPage_));
}

void MainWindowRound5Test::mainWindow_loadSearchData_beforePageDependenciesUse()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");
    QVERIFY2(sandbox.installRound2IndexFixture(), "round2 index fixture should be copied into sandbox");
    QVERIFY2(sandbox.writeCanonicalContentFixture(), "canonical content fixture should be written");
    QVERIFY2(sandbox.seedFavorite(QStringLiteral("X001")), "favorites fixture should be seeded");

    MainWindow window(nullptr);

    QVERIFY(window.isIndexReady());
    QVERIFY(window.isContentReady());
    QVERIFY(window.searchPage_ != nullptr);
    QVERIFY(window.settingsPage_ != nullptr);
    QVERIFY(window.searchPage_->indexReady_);
    QVERIFY(window.searchPage_->contentReady_);
    QVERIFY(window.settingsPage_->indexLoaded_);
    QVERIFY(window.settingsPage_->contentLoaded_);
    QVERIFY2(hasHomePreviewTitle(window.homePage_, QStringLiteral("Exact Match Core")),
             "home favorites preview should resolve indexed title instead of raw id");
}

void MainWindowRound5Test::mainWindow_switchPageWithTrigger_movesMainStack()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");
    QVERIFY2(sandbox.installRound2IndexFixture(), "round2 index fixture should be copied into sandbox");
    QVERIFY2(sandbox.writeCanonicalContentFixture(), "canonical content fixture should be written");

    MainWindow window(nullptr);
    QVERIFY(window.pageStack_ != nullptr);
    QCOMPARE(window.pageStack_->currentIndex(), UiConstants::kPageHome);

    QVERIFY(window.navigationSidebar_ != nullptr);
    window.navigationSidebar_->pageRequested(UiConstants::kPageSearch);
    QCOMPARE(window.pageStack_->currentIndex(), UiConstants::kPageSearch);
    QCOMPARE(window.currentPageIndex_, UiConstants::kPageSearch);

    window.navigationSidebar_->pageRequested(77);
    QCOMPARE(window.pageStack_->currentIndex(), UiConstants::kPageSearch);

    QVERIFY(QMetaObject::invokeMethod(&window, "switchPage", Qt::DirectConnection, Q_ARG(int, UiConstants::kPageSettings)));
    QCOMPARE(window.pageStack_->currentIndex(), UiConstants::kPageSettings);
    QCOMPARE(window.currentPageIndex_, UiConstants::kPageSettings);
}

void MainWindowRound5Test::crossPage_favoritesChanged_refreshesFavoritesAndHome()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");
    QVERIFY2(sandbox.installRound2IndexFixture(), "round2 index fixture should be copied into sandbox");
    QVERIFY2(sandbox.writeCanonicalContentFixture(), "canonical content fixture should be written");
    QVERIFY2(sandbox.writeFullLicenseForCurrentDevice(), "full license fixture should be written");

    MainWindow window(nullptr);
    QVERIFY(window.searchPage_ != nullptr);
    QVERIFY(window.favoritesPage_ != nullptr);

    QCOMPARE(window.favoritesPage_->findChildren<FavoriteItemCard*>().size(), 0);

    window.searchPage_->currentDetailDocId_ = QStringLiteral("X001");
    QSignalSpy favoritesChangedSpy(window.searchPage_, &SearchPage::favoritesChanged);
    QVERIFY(favoritesChangedSpy.isValid());
    QVERIFY(QMetaObject::invokeMethod(window.searchPage_, "onFavoriteButtonClicked", Qt::DirectConnection));

    QCOMPARE(favoritesChangedSpy.count(), 1);
    QTRY_COMPARE(window.favoritesPage_->findChildren<FavoriteItemCard*>().size(), 1);
    QTRY_VERIFY(hasHomePreviewTitle(window.homePage_, QStringLiteral("Exact Match Core")));
}

void MainWindowRound5Test::crossPage_recentClick_jumpsBackToSearch()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");
    QVERIFY2(sandbox.installRound2IndexFixture(), "round2 index fixture should be copied into sandbox");
    QVERIFY2(sandbox.writeCanonicalContentFixture(), "canonical content fixture should be written");
    QVERIFY2(sandbox.seedRecentQuery(QStringLiteral("exact term")), "recent history fixture should be seeded");

    MainWindow window(nullptr);
    QVERIFY(window.recentSearchesPage_ != nullptr);
    QVERIFY(window.searchPage_ != nullptr);
    QVERIFY(window.pageStack_ != nullptr);

    QVERIFY(QMetaObject::invokeMethod(
        &window, "switchPage", Qt::DirectConnection, Q_ARG(int, UiConstants::kPageRecentSearches)));
    QCOMPARE(window.pageStack_->currentIndex(), UiConstants::kPageRecentSearches);

    auto* firstItem = window.recentSearchesPage_->findChild<RecentSearchItemWidget*>();
    QVERIFY(firstItem != nullptr);

    QSignalSpy searchRequestedSpy(window.recentSearchesPage_, &RecentSearchesPage::searchRequested);
    QVERIFY(searchRequestedSpy.isValid());

    auto* searchAgainButton = firstItem->findChild<QPushButton*>(QStringLiteral("primaryButton"));
    QVERIFY(searchAgainButton != nullptr);
    QTest::mouseClick(searchAgainButton, Qt::LeftButton);

    QCOMPARE(searchRequestedSpy.count(), 1);
    QCOMPARE(searchRequestedSpy.at(0).at(0).toString(), QStringLiteral("exact term"));
    QTRY_COMPARE(window.pageStack_->currentIndex(), UiConstants::kPageSearch);

    auto* searchInput = window.searchPage_->findChild<QLineEdit*>(QStringLiteral("searchInput"));
    QVERIFY(searchInput != nullptr);
    QCOMPARE(searchInput->text().trimmed(), QStringLiteral("exact term"));
}

void MainWindowRound5Test::crossPage_favoritesOpenEntry_navigatesToSearchDetail()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");
    QVERIFY2(sandbox.installRound2IndexFixture(), "round2 index fixture should be copied into sandbox");
    QVERIFY2(sandbox.writeCanonicalContentFixture(), "canonical content fixture should be written");
    QVERIFY2(sandbox.writeFullLicenseForCurrentDevice(), "full license fixture should be written");
    QVERIFY2(sandbox.seedFavorite(QStringLiteral("X001")), "favorites fixture should be seeded");

    MainWindow window(nullptr);
    QVERIFY(window.favoritesPage_ != nullptr);
    QVERIFY(window.searchPage_ != nullptr);
    QVERIFY(window.pageStack_ != nullptr);

    QVERIFY(QMetaObject::invokeMethod(
        &window, "switchPage", Qt::DirectConnection, Q_ARG(int, UiConstants::kPageFavorites)));
    QCOMPARE(window.pageStack_->currentIndex(), UiConstants::kPageFavorites);

    auto* card = window.favoritesPage_->findChild<FavoriteItemCard*>();
    QVERIFY(card != nullptr);

    QSignalSpy openSpy(window.favoritesPage_, &FavoritesPage::openConclusionRequested);
    QVERIFY(openSpy.isValid());

    auto* openButton = card->findChild<QPushButton*>(QStringLiteral("primaryButton"));
    QVERIFY(openButton != nullptr);
    QTest::mouseClick(openButton, Qt::LeftButton);

    QCOMPARE(openSpy.count(), 1);
    QCOMPARE(openSpy.at(0).at(0).toString(), QStringLiteral("X001"));

    QTRY_COMPARE(window.pageStack_->currentIndex(), UiConstants::kPageSearch);
    QTRY_COMPARE(window.searchPage_->currentHits_.size(), 1);
    QCOMPARE(window.searchPage_->currentHits_.first().docId, QStringLiteral("X001"));
    QVERIFY(window.searchPage_->resultList_ != nullptr);
    QCOMPARE(window.searchPage_->resultList_->currentRow(), 0);
}

QTEST_MAIN(MainWindowRound5Test)

#include "test_main_window_round5.moc"
