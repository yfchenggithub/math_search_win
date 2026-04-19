#pragma once

#include "domain/models/conclusion_record.h"

#include <QHash>
#include <QString>
#include <QStringList>

namespace infrastructure::data {

struct CanonicalContentDiagnostics {
    QString fatalError;
    QStringList warnings;
    QStringList skippedRecordIds;
    qsizetype loadedCount = 0;
    qsizetype skippedCount = 0;

    bool isSuccess() const;
};

struct CanonicalContentLoadResult {
    QHash<QString, domain::models::ConclusionRecord> recordsById;
    CanonicalContentDiagnostics diagnostics;

    bool isSuccess() const;
};

class CanonicalContentLoader final {
public:
    static QString defaultCanonicalContentPath();
    static CanonicalContentLoadResult loadFromFile(const QString& filePath = QString());
};

}  // namespace infrastructure::data

