#include "domain/repositories/settings_repository.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"

#include <QFileInfo>
#include <QJsonObject>

namespace domain::repositories {
namespace {

QVariantMap defaultSettings()
{
    return domain::models::AppSettings::defaultValues();
}

}  // namespace

SettingsRepository::SettingsRepository(infrastructure::storage::LocalStorageService* storageService, bool autoSave)
    : storageService_(storageService == nullptr ? &ownedStorageService_ : storageService)
    , values_(defaultSettings())
    , autoSave_(autoSave)
{
}

bool SettingsRepository::load()
{
    values_ = defaultSettings();

    if (!storageService_->ensureCacheDirExists()) {
        return false;
    }

    const QString path = storageService_->settingsFilePath();
    const QFileInfo fileInfo(path);

    bool ok = false;
    const QJsonDocument document = storageService_->readJsonFile(path, &ok);
    if (!ok) {
        if (!fileInfo.exists()) {
            return save();
        }

        LOG_WARN(LogCategory::FileIo,
                 QStringLiteral("settings load fallback to defaults due to unreadable file path=%1").arg(path));
        save();
        return false;
    }

    if (!document.isObject()) {
        LOG_WARN(LogCategory::FileIo,
                 QStringLiteral("settings load fallback to defaults due to non-object root path=%1").arg(path));
        save();
        return false;
    }

    const QJsonObject root = document.object();
    if (!root.contains(QStringLiteral("version"))) {
        LOG_WARN(LogCategory::FileIo, QStringLiteral("settings json missing version field path=%1").arg(path));
    }

    QJsonObject valuesObject;
    if (root.value(QStringLiteral("values")).isObject()) {
        valuesObject = root.value(QStringLiteral("values")).toObject();
    } else {
        // Backward compatibility for accidental flat-object settings files.
        valuesObject = root;
        valuesObject.remove(QStringLiteral("version"));
    }

    for (auto it = valuesObject.constBegin(); it != valuesObject.constEnd(); ++it) {
        values_.insert(it.key(), it.value().toVariant());
    }
    return true;
}

bool SettingsRepository::save()
{
    QJsonObject valuesObject;
    for (auto it = values_.constBegin(); it != values_.constEnd(); ++it) {
        valuesObject.insert(it.key(), QJsonValue::fromVariant(it.value()));
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), domain::models::AppSettings::kVersion);
    root.insert(QStringLiteral("values"), valuesObject);
    return storageService_->writeJsonFileAtomically(storageService_->settingsFilePath(), QJsonDocument(root));
}

QVariant SettingsRepository::value(const QString& key, const QVariant& defaultValue) const
{
    const QString normalized = normalizeKey(key);
    if (normalized.isEmpty()) {
        return defaultValue;
    }
    if (values_.contains(normalized)) {
        return values_.value(normalized);
    }
    return defaultValue;
}

void SettingsRepository::setValue(const QString& key, const QVariant& value)
{
    const QString normalized = normalizeKey(key);
    if (normalized.isEmpty()) {
        return;
    }

    values_.insert(normalized, value);
    persistIfNeeded();
}

bool SettingsRepository::contains(const QString& key) const
{
    const QString normalized = normalizeKey(key);
    return !normalized.isEmpty() && values_.contains(normalized);
}

void SettingsRepository::remove(const QString& key)
{
    const QString normalized = normalizeKey(key);
    if (normalized.isEmpty() || !values_.contains(normalized)) {
        return;
    }

    values_.remove(normalized);
    persistIfNeeded();
}

void SettingsRepository::resetToDefaults()
{
    values_ = defaultSettings();
    persistIfNeeded();
}

void SettingsRepository::setAutoSave(bool enabled)
{
    autoSave_ = enabled;
}

bool SettingsRepository::autoSave() const
{
    return autoSave_;
}

QString SettingsRepository::normalizeKey(const QString& key)
{
    return key.trimmed();
}

void SettingsRepository::persistIfNeeded()
{
    if (!autoSave_) {
        return;
    }

    if (!save()) {
        LOG_WARN(LogCategory::FileIo, QStringLiteral("settings autosave failed path=%1").arg(storageService_->settingsFilePath()));
    }
}

}  // namespace domain::repositories

