#pragma once

#include "domain/models/search_history_item.h"
#include "domain/repositories/favorites_repository.h"
#include "domain/repositories/history_repository.h"

#include <QList>
#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace infrastructure::data {
class ConclusionIndexRepository;
}  // namespace infrastructure::data

class HomePage final : public QWidget {
    Q_OBJECT

public:
    explicit HomePage(const infrastructure::data::ConclusionIndexRepository* indexRepository,
                      QWidget* parent = nullptr);
    void reloadData();

signals:
    void navigateToSearchRequested();
    void navigateToRecentRequested();
    void navigateToFavoritesRequested();
    void navigateToSettingsRequested();
    void navigateToActivationRequested();
    void searchRequested(const QString& query);
    void openConclusionRequested(const QString& conclusionId);

private:
    struct FavoritePreviewItem {
        QString conclusionId;
        QString title;
        QString module;
    };

    void setupUi();
    void setupHeroSection();
    void setupQuickActionsSection();
    void setupPreviewSections();
    void setupConnections();
    void setupFooterSection();
    void rebuildRecentPreview();
    void rebuildFavoritesPreview();
    void updateQuickActionSummary();
    void updateActivationSummaryIfNeeded();
    void clearLayoutItems(QVBoxLayout* layout);
    static QString normalizedText(const QString& text, int maxLength = 44);

    const infrastructure::data::ConclusionIndexRepository* indexRepository_ = nullptr;
    domain::repositories::HistoryRepository historyRepository_;
    domain::repositories::FavoritesRepository favoritesRepository_;

    QList<domain::models::SearchHistoryItem> recentItems_;
    QList<FavoritePreviewItem> favoriteItems_;
    int recentItemCount_ = 0;
    int favoriteItemCount_ = 0;

    QVBoxLayout* rootLayout_ = nullptr;
    QScrollArea* scrollArea_ = nullptr;
    QWidget* contentWidget_ = nullptr;
    QVBoxLayout* contentLayout_ = nullptr;

    QWidget* heroWidget_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QLabel* subtitleLabel_ = nullptr;
    QPushButton* startSearchButton_ = nullptr;
    QPushButton* recentShortcutButton_ = nullptr;
    QPushButton* favoritesShortcutButton_ = nullptr;

    QWidget* quickActionsWidget_ = nullptr;
    QPushButton* recentQuickActionCard_ = nullptr;
    QPushButton* favoritesQuickActionCard_ = nullptr;
    QPushButton* settingsQuickActionCard_ = nullptr;
    QLabel* recentQuickActionDescription_ = nullptr;
    QLabel* favoritesQuickActionDescription_ = nullptr;
    QLabel* settingsQuickActionDescription_ = nullptr;

    QWidget* recentPreviewSection_ = nullptr;
    QWidget* favoritesPreviewSection_ = nullptr;
    QVBoxLayout* recentPreviewLayout_ = nullptr;
    QVBoxLayout* favoritesPreviewLayout_ = nullptr;
    QPushButton* viewAllRecentButton_ = nullptr;
    QPushButton* viewAllFavoritesButton_ = nullptr;

    QWidget* footerWidget_ = nullptr;
    QLabel* footerMetaLabel_ = nullptr;
    QPushButton* activationFooterButton_ = nullptr;
};
