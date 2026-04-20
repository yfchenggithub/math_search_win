#pragma once

#include "domain/models/search_history_item.h"
#include "infrastructure/storage/local_storage_service.h"

#include <QList>
#include <QString>

namespace domain::repositories {

class HistoryRepository final {
public:
    explicit HistoryRepository(infrastructure::storage::LocalStorageService* storageService = nullptr,
                               int maxItems = 100,
                               bool autoSave = true);

    bool load();
    bool save();

    void addQuery(const QString& query, const QString& source = QStringLiteral("manual"));
    QList<domain::models::SearchHistoryItem> recentItems(int limit = 20) const;

    void clear();
    int count() const;

    void setAutoSave(bool enabled);
    bool autoSave() const;

    void setMaxItems(int maxItems);
    int maxItems() const;

private:
    static QString normalizeQueryKey(const QString& query);
    static QString normalizeSource(const QString& source);

    void trimToMaxItems();
    void persistIfNeeded();

private:
    infrastructure::storage::LocalStorageService ownedStorageService_;
    infrastructure::storage::LocalStorageService* storageService_ = nullptr;
    QList<domain::models::SearchHistoryItem> items_;
    int maxItems_ = 100;
    bool autoSave_ = true;
};

}  // namespace domain::repositories

