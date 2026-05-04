#pragma once

#include "domain/models/domain_topic_map_models.h"

#include <QJsonParseError>
#include <QString>

namespace infrastructure::data {

struct DomainTopicMapDiagnostics {
    bool fileExists = false;
    QString filePath;
    QJsonParseError parseError;
    int domainCount = 0;
    int topicCount = 0;
    int aliasCount = 0;
};

class DomainTopicMapLoader final {
public:
    domain::models::DomainTopicMap loadFromFile(const QString& filePath);
    DomainTopicMapDiagnostics diagnostics() const;

private:
    DomainTopicMapDiagnostics diag_;

    void buildLookupIndexes(domain::models::DomainTopicMap* map);
};

}  // namespace infrastructure::data

