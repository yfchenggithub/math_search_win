#include "core/logging/logger.h"
#include "domain/repositories/favorites_repository.h"
#include "domain/repositories/history_repository.h"
#include "ui/pages/favorites_page.h"
#include "ui/pages/home_page.h"
#include "ui/pages/recent_searches_page.h"
#include "ui/widgets/favorites/favorite_item_card.h"
#include "ui/widgets/recent_search_item_widget.h"

#include <QtTest/QtTest>

#include <QDateTime>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSignalSpy>

namespace {

class ScopedSandboxRoot final {
public:
    ScopedSandboxRoot()
        : previousCwd_(QDir::currentPath())
    {
        const QString baseDir = QDir(previousCwd_).filePath(QStringLiteral(".tmp_ui_page_tests"));
        if (!QDir().mkpath(baseDir)) {
            return;
        }

        rootPath_ = QDir(baseDir).filePath(
            QStringLiteral("page_%1_%2")
                .arg(QDateTime::currentMSecsSinceEpoch())
                .arg(QRandomGenerator::global()->bounded(1000000)));
        if (!QDir().mkpath(rootPath_)) {
            rootPath_.clear();
            return;
        }

        QDir root(rootPath_);
        if (!root.mkpath(QStringLiteral("src")) || !root.mkpath(QStringLiteral("resources"))) {
            rootPath_.clear();
            return;
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
            && QDir(rootPath_).exists(QStringLiteral("resources"));
    }

private:
    QString previousCwd_;
    QString rootPath_;
};

QPushButton* findHomeActivationButton(const HomePage& page)
{
    const QList<QPushButton*> actionButtons = page.findChildren<QPushButton*>(QStringLiteral("homeSectionActionButton"));
    for (QPushButton* button : actionButtons) {
        if (button == nullptr || button->parentWidget() == nullptr) {
            continue;
        }

        const auto* footerMeta =
            button->parentWidget()->findChild<QLabel*>(QStringLiteral("homeFooterMeta"), Qt::FindDirectChildrenOnly);
        if (footerMeta != nullptr) {
            return button;
        }
    }
    return nullptr;
}

}  // namespace

class PageWiringTest final : public QObject {
    Q_OBJECT

private slots:
    void cleanupTestCase();

    void homePage_navigationButtons_emitSignals();
    void recentSearchesPage_researchDeleteClear_emitSignalsAndRefresh();
    void favoritesPage_openAndUnfavorite_emitSignalsAndRefresh();
};

void PageWiringTest::cleanupTestCase()
{
    logging::Logger::instance().shutdown();
}

void PageWiringTest::homePage_navigationButtons_emitSignals()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");

    HomePage page(nullptr);

    QSignalSpy searchSpy(&page, &HomePage::navigateToSearchRequested);
    QSignalSpy recentSpy(&page, &HomePage::navigateToRecentRequested);
    QSignalSpy favoritesSpy(&page, &HomePage::navigateToFavoritesRequested);
    QSignalSpy settingsSpy(&page, &HomePage::navigateToSettingsRequested);
    QSignalSpy activationSpy(&page, &HomePage::navigateToActivationRequested);

    auto* searchButton = page.findChild<QPushButton*>(QStringLiteral("homeHeroPrimaryButton"));
    QVERIFY(searchButton != nullptr);
    QTest::mouseClick(searchButton, Qt::LeftButton);
    QCOMPARE(searchSpy.count(), 1);

    const QList<QPushButton*> shortcutButtons = page.findChildren<QPushButton*>(QStringLiteral("homeHeroSecondaryButton"));
    QVERIFY(shortcutButtons.size() >= 2);
    QTest::mouseClick(shortcutButtons.at(0), Qt::LeftButton);
    QTest::mouseClick(shortcutButtons.at(1), Qt::LeftButton);
    QCOMPARE(recentSpy.count(), 1);
    QCOMPARE(favoritesSpy.count(), 1);

    QPushButton* settingsCard = nullptr;
    const QList<QPushButton*> quickCards = page.findChildren<QPushButton*>(QStringLiteral("homeQuickActionCard"));
    for (QPushButton* card : quickCards) {
        if (card != nullptr && card->property("weakEntry").toBool()) {
            settingsCard = card;
            break;
        }
    }
    QVERIFY(settingsCard != nullptr);
    QTest::mouseClick(settingsCard, Qt::LeftButton);
    QCOMPARE(settingsSpy.count(), 1);

    QPushButton* activationButton = findHomeActivationButton(page);
    QVERIFY(activationButton != nullptr);
    QTest::mouseClick(activationButton, Qt::LeftButton);
    QCOMPARE(activationSpy.count(), 1);
}

void PageWiringTest::recentSearchesPage_researchDeleteClear_emitSignalsAndRefresh()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");

    domain::repositories::HistoryRepository historyRepository;
    QVERIFY(historyRepository.load());
    historyRepository.addQuery(QStringLiteral("alpha"), QStringLiteral("manual"));
    historyRepository.addQuery(QStringLiteral("beta"), QStringLiteral("manual"));

    RecentSearchesPage page;

    QSignalSpy searchSpy(&page, &RecentSearchesPage::searchRequested);
    QSignalSpy historyChangedSpy(&page, &RecentSearchesPage::historyChanged);

    auto* firstItem = page.findChild<RecentSearchItemWidget*>();
    QVERIFY(firstItem != nullptr);

    auto* reSearchButton = firstItem->findChild<QPushButton*>(QStringLiteral("primaryButton"));
    QVERIFY(reSearchButton != nullptr);
    QTest::mouseClick(reSearchButton, Qt::LeftButton);

    QCOMPARE(searchSpy.count(), 1);
    QCOMPARE(historyChangedSpy.count(), 1);
    QVERIFY(!searchSpy.at(0).at(0).toString().trimmed().isEmpty());

    auto* deleteButton = firstItem->findChild<QPushButton*>(QStringLiteral("weakDangerButton"));
    QVERIFY(deleteButton != nullptr);
    QTest::mouseClick(deleteButton, Qt::LeftButton);

    QTRY_VERIFY(page.findChildren<RecentSearchItemWidget*>().size() <= 1);
    QCOMPARE(historyChangedSpy.count(), 2);

    auto* clearAllButton = page.findChild<QPushButton*>(QStringLiteral("secondaryButton"));
    QVERIFY(clearAllButton != nullptr);
    QTest::mouseClick(clearAllButton, Qt::LeftButton);

    QTRY_COMPARE(page.findChildren<RecentSearchItemWidget*>().size(), 0);
    QCOMPARE(historyChangedSpy.count(), 3);

    QVERIFY(historyRepository.load());
    QCOMPARE(historyRepository.count(), 0);
}

void PageWiringTest::favoritesPage_openAndUnfavorite_emitSignalsAndRefresh()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");

    domain::repositories::FavoritesRepository favoritesRepository;
    QVERIFY(favoritesRepository.load());
    favoritesRepository.add(QStringLiteral("A001"));

    FavoritesPage page(nullptr, nullptr, nullptr, nullptr);

    QSignalSpy openSpy(&page, &FavoritesPage::openConclusionRequested);
    QSignalSpy changedSpy(&page, &FavoritesPage::favoritesChanged);

    auto* card = page.findChild<FavoriteItemCard*>();
    QVERIFY(card != nullptr);

    auto* openButton = card->findChild<QPushButton*>(QStringLiteral("primaryButton"));
    QVERIFY(openButton != nullptr);
    QTest::mouseClick(openButton, Qt::LeftButton);

    QCOMPARE(openSpy.count(), 1);
    QCOMPARE(openSpy.at(0).at(0).toString(), QStringLiteral("A001"));

    auto* unfavoriteButton = card->findChild<QPushButton*>(QStringLiteral("weakDangerButton"));
    QVERIFY(unfavoriteButton != nullptr);
    QTest::mouseClick(unfavoriteButton, Qt::LeftButton);

    QCOMPARE(changedSpy.count(), 1);
    QTRY_COMPARE(page.findChildren<FavoriteItemCard*>().size(), 0);

    QVERIFY(favoritesRepository.load());
    QCOMPARE(favoritesRepository.count(), 0);
}

QTEST_MAIN(PageWiringTest)

#include "test_page_wiring.moc"
