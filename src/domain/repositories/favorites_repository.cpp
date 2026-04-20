#include "domain/repositories/favorites_repository.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

namespace domain::repositories {
namespace {

constexpr int kFavoritesSchemaVersion = 1;

}  // namespace

FavoritesRepository::FavoritesRepository(infrastructure::storage::LocalStorageService* storageService, bool autoSave)
    : storageService_(storageService == nullptr ? &ownedStorageService_ : storageService)
    , autoSave_(autoSave)
{
}

bool FavoritesRepository::load()
{
    favoriteIds_.clear();

    if (!storageService_->ensureCacheDirExists()) {
        return false;
    }

    const QString path = storageService_->favoritesFilePath();
    const QFileInfo fileInfo(path);

    bool ok = false;
    const QJsonDocument doc = storageService_->readJsonFile(path, &ok);
    if (!ok) {
        if (!fileInfo.exists()) {
            return save();
        }

        LOG_WARN(LogCategory::FileIo,
                 QStringLiteral("favorites load fallback to empty due to unreadable file path=%1").arg(path));
        save();
        return false;
    }

    if (!doc.isObject()) {
        LOG_WARN(LogCategory::FileIo,
                 QStringLiteral("favorites load fallback to empty due to non-object root path=%1").arg(path));
        save();
        return false;
    }

    const QJsonObject root = doc.object();
    if (!root.contains(QStringLiteral("version"))) {
        LOG_WARN(LogCategory::FileIo, QStringLiteral("favorites json missing version field path=%1").arg(path));
    }

    QSet<QString> dedupeSet;
    auto appendIfUnique = [&dedupeSet, this](const QString& rawId) {
        const QString id = normalizeId(rawId);
        if (id.isEmpty() || dedupeSet.contains(id)) {
            return;
        }
        dedupeSet.insert(id);
        favoriteIds_.push_back(id);
    };

    // MVP uses flat `ids` array for simplicity and low maintenance cost.
    // We also accept `items[].id` for forward/backward compatibility.
    const QJsonArray idsArray = root.value(QStringLiteral("ids")).toArray();
    for (const QJsonValue& value : idsArray) {
        appendIfUnique(value.toString());
    }

    const QJsonArray itemsArray = root.value(QStringLiteral("items")).toArray();
    for (const QJsonValue& value : itemsArray) {
        appendIfUnique(value.toObject().value(QStringLiteral("id")).toString());
    }

    return true;
}

bool FavoritesRepository::save()
{
    QJsonArray idsArray;
    for (const QString& id : favoriteIds_) {
        idsArray.push_back(id);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), kFavoritesSchemaVersion);
    root.insert(QStringLiteral("ids"), idsArray);
    return storageService_->writeJsonFileAtomically(storageService_->favoritesFilePath(), QJsonDocument(root));
}

bool FavoritesRepository::contains(const QString& conclusionId) const
{
    const QString id = normalizeId(conclusionId);
    return !id.isEmpty() && favoriteIds_.contains(id);
}

void FavoritesRepository::add(const QString& conclusionId)
{
    const QString id = normalizeId(conclusionId);
    if (id.isEmpty() || favoriteIds_.contains(id)) {
        return;
    }

    favoriteIds_.push_back(id);
    persistIfNeeded();
}

void FavoritesRepository::remove(const QString& conclusionId)
{
    const QString id = normalizeId(conclusionId);
    if (id.isEmpty()) {
        return;
    }

    if (favoriteIds_.removeAll(id) <= 0) {
        return;
    }

    persistIfNeeded();
}

void FavoritesRepository::toggle(const QString& conclusionId)
{
    if (contains(conclusionId)) {
        remove(conclusionId);
    } else {
        add(conclusionId);
    }
}

QStringList FavoritesRepository::allIds() const
{
    return favoriteIds_;
}

int FavoritesRepository::count() const
{
    return favoriteIds_.size();
}

void FavoritesRepository::clear()
{
    if (favoriteIds_.isEmpty()) {
        return;
    }
    favoriteIds_.clear();
    persistIfNeeded();
}

void FavoritesRepository::setAutoSave(bool enabled)
{
    autoSave_ = enabled;
}

bool FavoritesRepository::autoSave() const
{
    return autoSave_;
}

void FavoritesRepository::persistIfNeeded()
{
    if (!autoSave_) {
        return;
    }

    if (!save()) {
        LOG_WARN(LogCategory::FileIo, QStringLiteral("favorites autosave failed path=%1").arg(storageService_->favoritesFilePath()));
    }
}

QString FavoritesRepository::normalizeId(const QString& conclusionId)
{
    return conclusionId.trimmed();
}

}  // namespace domain::repositories
