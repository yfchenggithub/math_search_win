#pragma once

#include "infrastructure/storage/local_storage_service.h"

#include <QStringList>

namespace domain::repositories {

class FavoritesRepository final {
public:
    explicit FavoritesRepository(infrastructure::storage::LocalStorageService* storageService = nullptr,
                                 bool autoSave = true);

    bool load();
    bool save();

    bool contains(const QString& conclusionId) const;
    void add(const QString& conclusionId);
    void remove(const QString& conclusionId);
    void toggle(const QString& conclusionId);

    QStringList allIds() const;
    int count() const;
    void clear();

    void setAutoSave(bool enabled);
    bool autoSave() const;

private:
    void persistIfNeeded();
    static QString normalizeId(const QString& conclusionId);

private:
    infrastructure::storage::LocalStorageService ownedStorageService_;
    infrastructure::storage::LocalStorageService* storageService_ = nullptr;
    QStringList favoriteIds_;
    bool autoSave_ = true;
};

}  // namespace domain::repositories

