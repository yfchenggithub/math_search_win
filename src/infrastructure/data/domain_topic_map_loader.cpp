#include "infrastructure/data/domain_topic_map_loader.h"

#include "domain/models/search_result_models.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>

namespace infrastructure::data {
namespace {

QString readStringField(const QJsonObject& object, const QString& key)
{
    const QJsonValue value = object.value(key);
    if (!value.isString()) {
        return {};
    }
    return value.toString().trimmed();
}

QStringList readStringListValue(const QJsonValue& rawValue)
{
    QStringList values;
    if (!rawValue.isArray()) {
        return values;
    }

    const QJsonArray array = rawValue.toArray();
    values.reserve(array.size());
    for (qsizetype i = 0; i < array.size(); ++i) {
        if (!array.at(i).isString()) {
            continue;
        }
        const QString text = array.at(i).toString().trimmed();
        if (!text.isEmpty()) {
            values.push_back(text);
        }
    }
    return values;
}

QStringList readStringListField(const QJsonObject& object, const QString& key)
{
    return readStringListValue(object.value(key));
}

QStringList dedupeCaseInsensitive(const QStringList& values)
{
    QStringList deduped;
    QSet<QString> seen;
    seen.reserve(values.size());
    deduped.reserve(values.size());
    for (const QString& value : values) {
        const QString trimmed = value.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        const QString key = trimmed.toLower();
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        deduped.push_back(trimmed);
    }
    return deduped;
}

QStringList readDocIdsField(const QJsonObject& object)
{
    QStringList values;
    values.append(readStringListField(object, QStringLiteral("docIds")));
    values.append(readStringListField(object, QStringLiteral("docs")));
    return dedupeCaseInsensitive(values);
}

domain::models::TopicEntry parseTopicEntry(const QJsonObject& topicObject, const QString& fallbackName = QString())
{
    domain::models::TopicEntry topic;
    topic.name = readStringField(topicObject, QStringLiteral("name"));
    if (topic.name.isEmpty()) {
        topic.name = fallbackName.trimmed();
    }
    topic.aliases = dedupeCaseInsensitive(readStringListField(topicObject, QStringLiteral("aliases")));
    topic.docIds = readDocIdsField(topicObject);
    return topic;
}

QVector<domain::models::TopicEntry> parseTopics(const QJsonValue& rawTopics)
{
    QVector<domain::models::TopicEntry> topics;
    if (rawTopics.isArray()) {
        const QJsonArray topicArray = rawTopics.toArray();
        topics.reserve(topicArray.size());
        for (qsizetype ti = 0; ti < topicArray.size(); ++ti) {
            if (!topicArray.at(ti).isObject()) {
                continue;
            }
            domain::models::TopicEntry topic = parseTopicEntry(topicArray.at(ti).toObject());
            if (topic.name.isEmpty() && topic.aliases.isEmpty() && topic.docIds.isEmpty()) {
                continue;
            }
            topics.push_back(std::move(topic));
        }
        return topics;
    }

    if (!rawTopics.isObject()) {
        return topics;
    }

    const QJsonObject topicObjectMap = rawTopics.toObject();
    topics.reserve(topicObjectMap.size());
    for (auto it = topicObjectMap.constBegin(); it != topicObjectMap.constEnd(); ++it) {
        if (!it.value().isObject()) {
            continue;
        }
        domain::models::TopicEntry topic = parseTopicEntry(it.value().toObject(), it.key());
        if (topic.name.isEmpty() && topic.aliases.isEmpty() && topic.docIds.isEmpty()) {
            continue;
        }
        topics.push_back(std::move(topic));
    }
    return topics;
}

domain::models::DomainEntry parseDomainEntry(const QJsonObject& domainObject, const QString& fallbackName = QString())
{
    domain::models::DomainEntry domain;
    domain.name = readStringField(domainObject, QStringLiteral("name"));
    if (domain.name.isEmpty()) {
        domain.name = fallbackName.trimmed();
    }
    domain.aliases = dedupeCaseInsensitive(readStringListField(domainObject, QStringLiteral("aliases")));
    domain.topics = parseTopics(domainObject.value(QStringLiteral("topics")));
    return domain;
}

}  // namespace

domain::models::DomainTopicMap DomainTopicMapLoader::loadFromFile(const QString& filePath)
{
    diag_ = {};
    diag_.filePath = filePath.trimmed();

    domain::models::DomainTopicMap map;
    if (diag_.filePath.isEmpty()) {
        return map;
    }

    const QFileInfo fileInfo(diag_.filePath);
    diag_.fileExists = fileInfo.exists() && fileInfo.isFile();
    if (!diag_.fileExists) {
        return map;
    }

    QFile file(diag_.filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return map;
    }

    const QByteArray rawBytes = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument jsonDocument = QJsonDocument::fromJson(rawBytes, &parseError);
    diag_.parseError = parseError;
    if (parseError.error != QJsonParseError::NoError || !jsonDocument.isObject()) {
        return map;
    }

    const QJsonObject root = jsonDocument.object();
    const QJsonValue rawDomains = root.value(QStringLiteral("domains"));
    if (!rawDomains.isArray() && !rawDomains.isObject()) {
        return map;
    }

    if (rawDomains.isArray()) {
        const QJsonArray domains = rawDomains.toArray();
        map.domains.reserve(domains.size());
        for (qsizetype di = 0; di < domains.size(); ++di) {
            if (!domains.at(di).isObject()) {
                continue;
            }
            domain::models::DomainEntry domain = parseDomainEntry(domains.at(di).toObject());
            if (domain.name.isEmpty() && domain.aliases.isEmpty() && domain.topics.isEmpty()) {
                continue;
            }
            map.domains.push_back(std::move(domain));
        }
    } else {
        const QJsonObject domainObjectMap = rawDomains.toObject();
        map.domains.reserve(domainObjectMap.size());
        for (auto it = domainObjectMap.constBegin(); it != domainObjectMap.constEnd(); ++it) {
            if (!it.value().isObject()) {
                continue;
            }
            domain::models::DomainEntry domain = parseDomainEntry(it.value().toObject(), it.key());
            if (domain.name.isEmpty() && domain.aliases.isEmpty() && domain.topics.isEmpty()) {
                continue;
            }
            map.domains.push_back(std::move(domain));
        }
    }

    buildLookupIndexes(&map);
    return map;
}

DomainTopicMapDiagnostics DomainTopicMapLoader::diagnostics() const
{
    return diag_;
}

void DomainTopicMapLoader::buildLookupIndexes(domain::models::DomainTopicMap* map)
{
    if (map == nullptr) {
        return;
    }

    map->domainByAlias.clear();
    map->topicByAlias.clear();

    int aliasCount = 0;
    for (int domainIndex = 0; domainIndex < map->domains.size(); ++domainIndex) {
        const domain::models::DomainEntry& domain = map->domains.at(domainIndex);

        QStringList domainTokens = domain.aliases;
        domainTokens.push_back(domain.name);
        for (const QString& token : domainTokens) {
            const QString normalized = domain::models::normalizeQueryText(token);
            if (normalized.isEmpty() || map->domainByAlias.contains(normalized)) {
                continue;
            }
            map->domainByAlias.insert(normalized, domainIndex);
            ++aliasCount;
        }

        for (int topicIndex = 0; topicIndex < domain.topics.size(); ++topicIndex) {
            const domain::models::TopicEntry& topic = domain.topics.at(topicIndex);
            QStringList topicTokens = topic.aliases;
            topicTokens.push_back(topic.name);
            for (const QString& token : topicTokens) {
                const QString normalized = domain::models::normalizeQueryText(token);
                if (normalized.isEmpty() || map->topicByAlias.contains(normalized)) {
                    continue;
                }
                map->topicByAlias.insert(normalized, qMakePair(domainIndex, topicIndex));
                ++aliasCount;
            }
        }
    }

    int topicCount = 0;
    for (const domain::models::DomainEntry& domain : map->domains) {
        topicCount += domain.topics.size();
    }
    diag_.domainCount = map->domains.size();
    diag_.topicCount = topicCount;
    diag_.aliasCount = aliasCount;
}

}  // namespace infrastructure::data
