#pragma once

#include "domain/models/app_settings.h"
#include "infrastructure/storage/local_storage_service.h"

#include <QVariant>
#include <QVariantMap>

namespace domain::repositories {

class SettingsRepository final {
public:
    explicit SettingsRepository(infrastructure::storage::LocalStorageService* storageService = nullptr,
                                bool autoSave = true);

    bool load();
    bool save();

    QVariant value(const QString& key, const QVariant& defaultValue = {}) const;
    void setValue(const QString& key, const QVariant& value);
    bool contains(const QString& key) const;
    void remove(const QString& key);
    void resetToDefaults();

    void setAutoSave(bool enabled);
    bool autoSave() const;

private:
    static QString normalizeKey(const QString& key);
    void persistIfNeeded();

private:
    infrastructure::storage::LocalStorageService ownedStorageService_;
    infrastructure::storage::LocalStorageService* storageService_ = nullptr;
    QVariantMap values_;
    bool autoSave_ = true;
};

}  // namespace domain::repositories

