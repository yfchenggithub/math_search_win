#pragma once

#include "domain/models/search_index_models.h"

#include <QString>
#include <QStringList>

namespace infrastructure::data {

struct BackendSearchIndexDiagnostics {
    bool success = false;
    QString fatalError;
    QStringList warnings;
    qsizetype loadedDocCount = 0;
    qsizetype skippedDocCount = 0;
    qsizetype loadedTermCount = 0;
    qsizetype loadedPrefixCount = 0;
    qsizetype skippedPostingCount = 0;

    bool isSuccess() const;
};

struct BackendSearchIndexLoadResult {
    domain::models::BackendSearchIndex index;
    BackendSearchIndexDiagnostics diagnostics;

    bool isSuccess() const;
};

class BackendSearchIndexLoader final {
public:
    static QString defaultIndexPath();
    static BackendSearchIndexLoadResult loadFromFile(const QString& filePath = QString());
};

}  // namespace infrastructure::data

