#include "domain/repositories/history_repository.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTimeZone>

namespace domain::repositories {
namespace {

constexpr int kHistorySchemaVersion = 1;
constexpr int kMinHistorySize = 1;
constexpr int kDefaultHistorySize = 100;

QDateTime parseIsoDateTime(const QString& raw)
{
    if (raw.trimmed().isEmpty()) {
        return {};
    }

    QDateTime dt = QDateTime::fromString(raw, Qt::ISODateWithMs);
    if (dt.isValid()) {
        return dt;
    }

    dt = QDateTime::fromString(raw, Qt::ISODate);
    return dt;
}

QJsonObject toJsonObject(const domain::models::SearchHistoryItem& item)
{
    QJsonObject object;
    object.insert(QStringLiteral("query"), item.query);
    object.insert(QStringLiteral("source"), item.source);
    object.insert(QStringLiteral("searched_at"), item.searchedAt.toString(Qt::ISODateWithMs));
    return object;
}

bool fromJsonObject(const QJsonValue& value, domain::models::SearchHistoryItem* outItem)
{
    if (outItem == nullptr || !value.isObject()) {
        return false;
    }

    const QJsonObject object = value.toObject();
    domain::models::SearchHistoryItem item;
    item.query = object.value(QStringLiteral("query")).toString().trimmed();
    if (item.query.isEmpty()) {
        return false;
    }

    item.source = object.value(QStringLiteral("source")).toString().trimmed();
    if (item.source.isEmpty()) {
        item.source = QStringLiteral("manual");
    }

    item.searchedAt = parseIsoDateTime(object.value(QStringLiteral("searched_at")).toString());
    if (!item.searchedAt.isValid()) {
        item.searchedAt = QDateTime::fromSecsSinceEpoch(0, QTimeZone::UTC);
    }

    *outItem = item;
    return true;
}

}  // namespace

HistoryRepository::HistoryRepository(infrastructure::storage::LocalStorageService* storageService,
                                     int maxItems,
                                     bool autoSave)
    : storageService_(storageService == nullptr ? &ownedStorageService_ : storageService)
    , maxItems_(std::max(maxItems, kMinHistorySize))
    , autoSave_(autoSave)
{
    if (maxItems <= 0) {
        maxItems_ = kDefaultHistorySize;
    }
}

bool HistoryRepository::load()
{
    items_.clear();

    if (!storageService_->ensureCacheDirExists()) {
        return false;
    }

    const QString path = storageService_->historyFilePath();
    const QFileInfo fileInfo(path);

    bool ok = false;
    const QJsonDocument document = storageService_->readJsonFile(path, &ok);
    if (!ok) {
        if (!fileInfo.exists()) {
            return save();
        }

        LOG_WARN(LogCategory::FileIo, QStringLiteral("history load fallback to empty due to unreadable file path=%1").arg(path));
        save();
        return false;
    }

    if (!document.isObject()) {
        LOG_WARN(LogCategory::FileIo, QStringLiteral("history load fallback to empty due to non-object root path=%1").arg(path));
        save();
        return false;
    }

    const QJsonObject root = document.object();
    if (!root.contains(QStringLiteral("version"))) {
        LOG_WARN(LogCategory::FileIo, QStringLiteral("history json missing version field path=%1").arg(path));
    }

    const QJsonArray itemsArray = root.value(QStringLiteral("items")).toArray();
    QSet<QString> seenQueries;
    QList<domain::models::SearchHistoryItem> parsedItems;
    parsedItems.reserve(itemsArray.size());

    for (const QJsonValue& value : itemsArray) {
        domain::models::SearchHistoryItem item;
        if (!fromJsonObject(value, &item)) {
            continue;
        }

        const QString key = normalizeQueryKey(item.query);
        if (key.isEmpty()) {
            continue;
        }

        if (!seenQueries.contains(key)) {
            seenQueries.insert(key);
            parsedItems.push_back(item);
        }
    }

    items_ = std::move(parsedItems);
    trimToMaxItems();
    return true;
}

bool HistoryRepository::save()
{
    QJsonArray itemsArray;
    for (const domain::models::SearchHistoryItem& item : items_) {
        itemsArray.push_back(toJsonObject(item));
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), kHistorySchemaVersion);
    root.insert(QStringLiteral("items"), itemsArray);
    return storageService_->writeJsonFileAtomically(storageService_->historyFilePath(), QJsonDocument(root));
}

void HistoryRepository::addQuery(const QString& query, const QString& source)
{
    const QString normalizedQuery = query.trimmed();
    if (normalizedQuery.isEmpty()) {
        return;
    }

    const QString queryKey = normalizeQueryKey(normalizedQuery);
    for (qsizetype i = 0; i < items_.size(); ++i) {
        if (normalizeQueryKey(items_.at(i).query) == queryKey) {
            items_.removeAt(i);
            break;
        }
    }

    domain::models::SearchHistoryItem item;
    item.query = normalizedQuery;
    item.source = normalizeSource(source);
    item.searchedAt = QDateTime::currentDateTime();
    items_.prepend(item);
    trimToMaxItems();
    persistIfNeeded();
}

QList<domain::models::SearchHistoryItem> HistoryRepository::recentItems(int limit) const
{
    if (limit <= 0 || items_.isEmpty()) {
        return {};
    }
    if (limit >= items_.size()) {
        return items_;
    }
    return items_.mid(0, limit);
}

void HistoryRepository::clear()
{
    if (items_.isEmpty()) {
        return;
    }

    items_.clear();
    persistIfNeeded();
}

int HistoryRepository::count() const
{
    return items_.size();
}

void HistoryRepository::setAutoSave(bool enabled)
{
    autoSave_ = enabled;
}

bool HistoryRepository::autoSave() const
{
    return autoSave_;
}

void HistoryRepository::setMaxItems(int maxItems)
{
    maxItems_ = maxItems > 0 ? maxItems : kDefaultHistorySize;
    trimToMaxItems();
    persistIfNeeded();
}

int HistoryRepository::maxItems() const
{
    return maxItems_;
}

QString HistoryRepository::normalizeQueryKey(const QString& query)
{
    return query.trimmed().toCaseFolded();
}

QString HistoryRepository::normalizeSource(const QString& source)
{
    const QString normalized = source.trimmed();
    return normalized.isEmpty() ? QStringLiteral("manual") : normalized;
}

void HistoryRepository::trimToMaxItems()
{
    if (maxItems_ < kMinHistorySize) {
        maxItems_ = kDefaultHistorySize;
    }
    while (items_.size() > maxItems_) {
        items_.removeLast();
    }
}

void HistoryRepository::persistIfNeeded()
{
    if (!autoSave_) {
        return;
    }

    if (!save()) {
        LOG_WARN(LogCategory::FileIo, QStringLiteral("history autosave failed path=%1").arg(storageService_->historyFilePath()));
    }
}

}  // namespace domain::repositories
