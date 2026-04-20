#pragma once

#include "domain/models/search_history_item.h"
#include "domain/repositories/history_repository.h"

#include <QDateTime>
#include <QList>
#include <QWidget>

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

class RecentSearchesPage final : public QWidget {
    Q_OBJECT

public:
    explicit RecentSearchesPage(QWidget* parent = nullptr);
    void reloadData();

signals:
    void searchRequested(const QString& query);
    void navigateToSearchRequested();
    void historyChanged();

private slots:
    void handleClearAll();

private:
    void setupUi();
    void setupConnections();
    void rebuildList();
    void clearListItems();
    void updateEmptyState();
    void handleItemDelete(const QString& entryId);
    void handleSearchAgain(const QString& query);
    static QString formatRelativeTime(const QDateTime& timestamp);
    static bool applyAppStyleSheetOnce();

    domain::repositories::HistoryRepository historyRepository_;
    QList<domain::models::SearchHistoryItem> items_;

    QVBoxLayout* rootLayout_ = nullptr;
    QWidget* headerWidget_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QLabel* subtitleLabel_ = nullptr;
    QPushButton* clearAllButton_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    QScrollArea* scrollArea_ = nullptr;
    QWidget* listContainer_ = nullptr;
    QVBoxLayout* listLayout_ = nullptr;
    QWidget* emptyStateWidget_ = nullptr;
    QLabel* emptyTitleLabel_ = nullptr;
    QLabel* emptyDescriptionLabel_ = nullptr;
    QPushButton* emptyActionButton_ = nullptr;
};
