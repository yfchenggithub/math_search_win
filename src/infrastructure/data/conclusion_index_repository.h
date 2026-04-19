#pragma once

#include "domain/models/search_index_models.h"
#include "infrastructure/data/backend_search_index_loader.h"

#include <QString>
#include <QStringList>
#include <utility>

namespace infrastructure::data {

class ConclusionIndexRepository final {
public:
    using DiagnosticsType = BackendSearchIndexDiagnostics;

    bool loadFromFile(const QString& filePath = QString());

    const domain::models::IndexDocRecord* getDocById(const QString& docId) const;
    const QVector<domain::models::PostingEntry>* findTerm(const QString& key) const;
    const QVector<domain::models::PostingEntry>* findPrefix(const QString& prefix) const;
    bool containsDoc(const QString& docId) const;

    qsizetype docCount() const;
    qsizetype termCount() const;
    qsizetype prefixCount() const;
    qsizetype suggestionCount() const;

    QStringList modules() const;
    QStringList availableFieldNames() const;
    const DiagnosticsType& diagnostics() const;
    QString activeIndexPath() const;

    const domain::models::FieldMaskLegend& fieldMaskLegend() const;
    const QVector<domain::models::IndexedSuggestionSeed>& optionalSuggestions() const;

    template <typename Visitor>
    void forEachPrefixEntry(Visitor&& visitor) const
    {
        for (auto it = index_.prefixIndex.constBegin(); it != index_.prefixIndex.constEnd(); ++it) {
            std::forward<Visitor>(visitor)(it.key(), it.value());
        }
    }

    template <typename Visitor>
    void forEachTermEntry(Visitor&& visitor) const
    {
        for (auto it = index_.termIndex.constBegin(); it != index_.termIndex.constEnd(); ++it) {
            std::forward<Visitor>(visitor)(it.key(), it.value());
        }
    }

private:
    void rebuildAggregates();
    static QStringList uniqueAndSortCaseInsensitive(const QStringList& values);

private:
    domain::models::BackendSearchIndex index_;
    QStringList modules_;
    QStringList availableFieldNames_;
    DiagnosticsType diagnostics_;
    QString activeIndexPath_;
};

}  // namespace infrastructure::data

