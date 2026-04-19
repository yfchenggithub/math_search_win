#include "infrastructure/data/conclusion_index_repository.h"

#include <QSet>

#include <algorithm>

namespace infrastructure::data {
namespace {

QStringList sortFieldNamesByBit(const domain::models::FieldMaskLegend& legend)
{
    struct NameAndBit {
        QString name;
        quint32 bit = 0;
    };

    QVector<NameAndBit> rows;
    rows.reserve(legend.size());
    for (auto it = legend.constBegin(); it != legend.constEnd(); ++it) {
        rows.push_back({it.key(), it.value()});
    }

    std::sort(rows.begin(), rows.end(), [](const NameAndBit& lhs, const NameAndBit& rhs) {
        if (lhs.bit != rhs.bit) {
            return lhs.bit < rhs.bit;
        }
        const int compare = lhs.name.compare(rhs.name, Qt::CaseInsensitive);
        return compare == 0 ? lhs.name < rhs.name : compare < 0;
    });

    QStringList names;
    names.reserve(rows.size());
    for (const NameAndBit& row : rows) {
        names.push_back(row.name);
    }
    return names;
}

}  // namespace

bool ConclusionIndexRepository::loadFromFile(const QString& filePath)
{
    const QString resolvedPath = filePath.trimmed().isEmpty() ? BackendSearchIndexLoader::defaultIndexPath()
                                                               : filePath.trimmed();

    BackendSearchIndexLoadResult loadResult = BackendSearchIndexLoader::loadFromFile(resolvedPath);
    diagnostics_ = loadResult.diagnostics;
    if (!loadResult.isSuccess()) {
        index_ = {};
        modules_.clear();
        availableFieldNames_.clear();
        activeIndexPath_.clear();
        return false;
    }

    index_ = std::move(loadResult.index);
    activeIndexPath_ = resolvedPath;
    rebuildAggregates();
    return true;
}

const domain::models::IndexDocRecord* ConclusionIndexRepository::getDocById(const QString& docId) const
{
    const QString normalizedId = docId.trimmed();
    if (normalizedId.isEmpty()) {
        return nullptr;
    }
    const auto it = index_.docs.constFind(normalizedId);
    if (it == index_.docs.constEnd()) {
        return nullptr;
    }
    return &it.value();
}

const QVector<domain::models::PostingEntry>* ConclusionIndexRepository::findTerm(const QString& key) const
{
    const QString normalizedKey = key.trimmed();
    if (normalizedKey.isEmpty()) {
        return nullptr;
    }
    const auto it = index_.termIndex.constFind(normalizedKey);
    if (it == index_.termIndex.constEnd()) {
        return nullptr;
    }
    return &it.value();
}

const QVector<domain::models::PostingEntry>* ConclusionIndexRepository::findPrefix(const QString& prefix) const
{
    const QString normalizedPrefix = prefix.trimmed();
    if (normalizedPrefix.isEmpty()) {
        return nullptr;
    }
    const auto it = index_.prefixIndex.constFind(normalizedPrefix);
    if (it == index_.prefixIndex.constEnd()) {
        return nullptr;
    }
    return &it.value();
}

bool ConclusionIndexRepository::containsDoc(const QString& docId) const
{
    const QString normalizedId = docId.trimmed();
    return !normalizedId.isEmpty() && index_.docs.contains(normalizedId);
}

qsizetype ConclusionIndexRepository::docCount() const
{
    return index_.docs.size();
}

qsizetype ConclusionIndexRepository::termCount() const
{
    return index_.termIndex.size();
}

qsizetype ConclusionIndexRepository::prefixCount() const
{
    return index_.prefixIndex.size();
}

qsizetype ConclusionIndexRepository::suggestionCount() const
{
    if (!index_.suggestions.isEmpty()) {
        return index_.suggestions.size();
    }
    return index_.stats.suggestions;
}

QStringList ConclusionIndexRepository::modules() const
{
    return modules_;
}

QStringList ConclusionIndexRepository::availableFieldNames() const
{
    return availableFieldNames_;
}

const ConclusionIndexRepository::DiagnosticsType& ConclusionIndexRepository::diagnostics() const
{
    return diagnostics_;
}

QString ConclusionIndexRepository::activeIndexPath() const
{
    return activeIndexPath_;
}

const domain::models::FieldMaskLegend& ConclusionIndexRepository::fieldMaskLegend() const
{
    return index_.fieldMaskLegend;
}

const QVector<domain::models::IndexedSuggestionSeed>& ConclusionIndexRepository::optionalSuggestions() const
{
    return index_.suggestions;
}

void ConclusionIndexRepository::rebuildAggregates()
{
    QStringList modules;
    modules.reserve(index_.docs.size());

    for (auto it = index_.docs.constBegin(); it != index_.docs.constEnd(); ++it) {
        const QString module = it.value().module.trimmed();
        if (!module.isEmpty()) {
            modules.push_back(module);
        }
    }

    modules_ = uniqueAndSortCaseInsensitive(modules);
    availableFieldNames_ = sortFieldNamesByBit(index_.fieldMaskLegend);
}

QStringList ConclusionIndexRepository::uniqueAndSortCaseInsensitive(const QStringList& values)
{
    QSet<QString> set;
    set.reserve(values.size());
    for (const QString& value : values) {
        set.insert(value);
    }

    QStringList sorted = set.values();
    std::sort(sorted.begin(), sorted.end(), [](const QString& lhs, const QString& rhs) {
        const int compare = lhs.compare(rhs, Qt::CaseInsensitive);
        return compare == 0 ? lhs < rhs : compare < 0;
    });
    return sorted;
}

}  // namespace infrastructure::data

