#pragma once

#include "domain/repositories/favorites_repository.h"
#include "infrastructure/storage/local_storage_service.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace infrastructure::data {
class ConclusionContentRepository;
class ConclusionIndexRepository;
}  // namespace infrastructure::data

class FavoritesPage final : public QWidget {
    Q_OBJECT

public:
    explicit FavoritesPage(const infrastructure::data::ConclusionContentRepository* contentRepository,
                           const infrastructure::data::ConclusionIndexRepository* indexRepository,
                           QWidget* parent = nullptr);

    void reloadData();

signals:
    void openConclusionRequested(const QString& conclusionId);
    void navigateToSearchRequested();
    void favoritesChanged();

private:
    enum class SortMode {
        RecentFirst = 0,
        TitleAsc,
        ModuleAsc,
    };

    struct FavoriteItem {
        QString conclusionId;
        QString title;
        QString module;
        QStringList tags;
        QString difficultyText;
        QString summaryText;
        QDateTime favoriteAt;
        QString favoriteTimeText;
        int sourceOrder = -1;
    };

    void setupUi();
    void setupConnections();
    void applySort();
    void rebuildCards();
    void clearCards();
    void updateEmptyState();
    void rebuildFavoriteTimestampIndex();
    FavoriteItem buildItemFromId(const QString& conclusionId, int sourceOrder) const;
    static QString preferredSummary(const QString& statement, const QString& summaryCandidate);
    static QString normalizedText(const QString& text);
    static QDateTime parseDateTime(const QString& rawText);

private:
    domain::repositories::FavoritesRepository favoritesRepository_;
    infrastructure::storage::LocalStorageService localStorageService_;
    const infrastructure::data::ConclusionContentRepository* contentRepository_ = nullptr;
    const infrastructure::data::ConclusionIndexRepository* indexRepository_ = nullptr;

    QList<FavoriteItem> items_;
    QHash<QString, QDateTime> favoriteTimestampById_;

    QVBoxLayout* rootLayout_ = nullptr;
    QWidget* headerWidget_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QLabel* subtitleLabel_ = nullptr;
    QWidget* toolbarWidget_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    QComboBox* sortComboBox_ = nullptr;
    QPushButton* filterButton_ = nullptr;
    QScrollArea* scrollArea_ = nullptr;
    QWidget* cardsContainer_ = nullptr;
    QVBoxLayout* cardsLayout_ = nullptr;
    QWidget* emptyStateWidget_ = nullptr;
    QPushButton* emptyActionButton_ = nullptr;
};
