#pragma once

#include <QHash>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

namespace domain::models {

struct TopicEntry {
    QString name;
    QStringList aliases;
    QStringList docIds;
};

struct DomainEntry {
    QString name;
    QStringList aliases;
    QVector<TopicEntry> topics;
};

struct DomainTopicMap {
    QVector<DomainEntry> domains;
    QHash<QString, int> domainByAlias;
    QHash<QString, QPair<int, int>> topicByAlias;

    bool isEmpty() const { return domains.isEmpty(); }
};

}  // namespace domain::models

