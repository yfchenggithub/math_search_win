#include "infrastructure/data/conclusion_content_repository.h"

#include <QSet>

#include <algorithm>

namespace infrastructure::data {

bool ConclusionContentRepository::loadFromFile(const QString& filePath)
{
    const QString resolvedPath = filePath.trimmed().isEmpty() ? CanonicalContentLoader::defaultCanonicalContentPath()
                                                               : filePath.trimmed();
    CanonicalContentLoadResult loadResult = CanonicalContentLoader::loadFromFile(resolvedPath);
    diagnostics_ = loadResult.diagnostics;

    if (!loadResult.isSuccess()) {
        recordsById_.clear();
        orderedIds_.clear();
        modules_.clear();
        tags_.clear();
        activeContentPath_.clear();
        return false;
    }

    recordsById_ = std::move(loadResult.recordsById);
    activeContentPath_ = resolvedPath;

    orderedIds_ = recordsById_.keys();
    std::sort(orderedIds_.begin(), orderedIds_.end(), [](const QString& lhs, const QString& rhs) {
        const int compare = lhs.compare(rhs, Qt::CaseInsensitive);
        return compare == 0 ? lhs < rhs : compare < 0;
    });

    rebuildAggregates();
    return true;
}

const domain::models::ConclusionRecord* ConclusionContentRepository::getById(const QString& conclusionId) const
{
    const QString normalizedId = conclusionId.trimmed();
    if (normalizedId.isEmpty()) {
        return nullptr;
    }
    const auto it = recordsById_.constFind(normalizedId);
    if (it == recordsById_.constEnd()) {
        return nullptr;
    }
    return &it.value();
}

bool ConclusionContentRepository::contains(const QString& conclusionId) const
{
    const QString normalizedId = conclusionId.trimmed();
    return !normalizedId.isEmpty() && recordsById_.contains(normalizedId);
}

qsizetype ConclusionContentRepository::size() const
{
    return recordsById_.size();
}

QStringList ConclusionContentRepository::modules() const
{
    return modules_;
}

QStringList ConclusionContentRepository::tags() const
{
    return tags_;
}

const CanonicalContentDiagnostics& ConclusionContentRepository::diagnostics() const
{
    return diagnostics_;
}

QString ConclusionContentRepository::activeContentPath() const
{
    return activeContentPath_;
}

void ConclusionContentRepository::rebuildAggregates()
{
    QStringList moduleValues;
    QStringList tagValues;
    moduleValues.reserve(recordsById_.size());
    tagValues.reserve(recordsById_.size() * 3);

    for (auto it = recordsById_.cbegin(); it != recordsById_.cend(); ++it) {
        const domain::models::ConclusionRecord& record = it.value();

        const QString moduleValue = record.identity.module.trimmed();
        if (!moduleValue.isEmpty()) {
            moduleValues.push_back(moduleValue);
        }

        for (const QString& tag : record.meta.tags) {
            const QString normalizedTag = tag.trimmed();
            if (!normalizedTag.isEmpty()) {
                tagValues.push_back(normalizedTag);
            }
        }
    }

    modules_ = uniqueAndSortCaseInsensitive(moduleValues);
    tags_ = uniqueAndSortCaseInsensitive(tagValues);
}

QStringList ConclusionContentRepository::uniqueAndSortCaseInsensitive(const QStringList& values)
{
    QSet<QString> dedupe;
    dedupe.reserve(values.size());
    for (const QString& value : values) {
        dedupe.insert(value);
    }

    QStringList sorted = dedupe.values();
    std::sort(sorted.begin(), sorted.end(), [](const QString& lhs, const QString& rhs) {
        const int compare = lhs.compare(rhs, Qt::CaseInsensitive);
        return compare == 0 ? lhs < rhs : compare < 0;
    });
    return sorted;
}

}  // namespace infrastructure::data

