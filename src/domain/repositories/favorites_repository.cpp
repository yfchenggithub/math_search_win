#include "domain/repositories/favorites_repository.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

namespace domain::repositories {
namespace {

constexpr int kFavoritesSchemaVersion = 1;

QDateTime parseFavoriteTimestamp(const QJsonObject& object)
{
    const auto parseValue = [](const QJsonValue& value) -> QDateTime {
        const QString text = value.toString().trimmed();
        if (text.isEmpty()) {
            return {};
        }

        bool epochOk = false;
        const qint64 epoch = text.toLongLong(&epochOk);
        if (epochOk) {
            if (epoch > 1000000000000LL) {
                return QDateTime::fromMSecsSinceEpoch(epoch, Qt::UTC);
            }
            if (epoch > 1000000000LL) {
                return QDateTime::fromSecsSinceEpoch(epoch, Qt::UTC);
            }
        }

        QDateTime parsed = QDateTime::fromString(text, Qt::ISODateWithMs);
        if (parsed.isValid()) {
            return parsed.toUTC();
        }

        parsed = QDateTime::fromString(text, Qt::ISODate);
        if (parsed.isValid()) {
            return parsed.toUTC();
        }

        return {};
    };

    QDateTime timestamp = parseValue(object.value(QStringLiteral("favoritedAt")));
    if (!timestamp.isValid()) {
        timestamp = parseValue(object.value(QStringLiteral("updatedAt")));
    }
    if (!timestamp.isValid()) {
        timestamp = parseValue(object.value(QStringLiteral("createdAt")));
    }
    return timestamp;
}

QString toStorageTimestamp(const QDateTime& timestamp)
{
    return timestamp.isValid() ? timestamp.toUTC().toString(Qt::ISODateWithMs) : QString();
}

}  // namespace

FavoritesRepository::FavoritesRepository(infrastructure::storage::LocalStorageService* storageService, bool autoSave)
    : storageService_(storageService == nullptr ? &ownedStorageService_ : storageService)
    , autoSave_(autoSave)
{
}

bool FavoritesRepository::load()
{
    favoriteIds_.clear();
    favoriteTimestampsById_.clear();

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
    auto mergeTimestampIfNeeded = [this](const QString& id, const QDateTime& candidate) {
        if (id.isEmpty() || !candidate.isValid()) {
            return;
        }
        const auto existing = favoriteTimestampsById_.constFind(id);
        if (existing == favoriteTimestampsById_.constEnd() || !existing.value().isValid() || candidate > existing.value()) {
            favoriteTimestampsById_.insert(id, candidate);
        }
    };

    // Unified schema writes both `ids` and `items` (with timestamps).
    // Keep compatibility with legacy files containing either side.
    const QJsonArray idsArray = root.value(QStringLiteral("ids")).toArray();
    for (const QJsonValue& value : idsArray) {
        appendIfUnique(value.toString());
    }

    const QJsonArray itemsArray = root.value(QStringLiteral("items")).toArray();
    for (const QJsonValue& value : itemsArray) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject itemObject = value.toObject();
        const QString id = itemObject.value(QStringLiteral("id")).toString();
        appendIfUnique(id);
        mergeTimestampIfNeeded(normalizeId(id), parseFavoriteTimestamp(itemObject));
    }

    return true;
}

bool FavoritesRepository::save()
{
    QJsonArray idsArray;
    QJsonArray itemsArray;
    for (const QString& id : favoriteIds_) {
        idsArray.push_back(id);

        QJsonObject itemObject;
        itemObject.insert(QStringLiteral("id"), id);
        const auto timestampIt = favoriteTimestampsById_.constFind(id);
        if (timestampIt != favoriteTimestampsById_.constEnd() && timestampIt.value().isValid()) {
            const QString timestamp = toStorageTimestamp(timestampIt.value());
            itemObject.insert(QStringLiteral("favoritedAt"), timestamp);
            itemObject.insert(QStringLiteral("updatedAt"), timestamp);
        }
        itemsArray.push_back(itemObject);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), kFavoritesSchemaVersion);
    root.insert(QStringLiteral("ids"), idsArray);
    root.insert(QStringLiteral("items"), itemsArray);
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
    favoriteTimestampsById_.insert(id, QDateTime::currentDateTimeUtc());
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

    favoriteTimestampsById_.remove(id);
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
    favoriteTimestampsById_.clear();
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
