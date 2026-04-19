#pragma once

#include "domain/models/conclusion_record.h"
#include "infrastructure/data/canonical_content_loader.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <utility>

namespace infrastructure::data {

class ConclusionContentRepository final {
public:
    bool loadFromFile(const QString& filePath = QString());

    const domain::models::ConclusionRecord* getById(const QString& conclusionId) const;
    bool contains(const QString& conclusionId) const;
    qsizetype size() const;

    QStringList modules() const;
    QStringList tags() const;

    const CanonicalContentDiagnostics& diagnostics() const;
    QString activeContentPath() const;

    // Core traversal API (function_ref-style call semantics) to avoid materializing copied record lists.
    template <typename Visitor>
    void forEachRecord(Visitor&& visitor) const
    {
        for (const QString& id : orderedIds_) {
            const auto it = recordsById_.constFind(id);
            if (it != recordsById_.constEnd()) {
                std::forward<Visitor>(visitor)(id, it.value());
            }
        }
    }

private:
    void rebuildAggregates();
    static QStringList uniqueAndSortCaseInsensitive(const QStringList& values);

private:
    QHash<QString, domain::models::ConclusionRecord> recordsById_;
    QStringList orderedIds_;
    QStringList modules_;
    QStringList tags_;
    CanonicalContentDiagnostics diagnostics_;
    QString activeContentPath_;
};

}  // namespace infrastructure::data

