#include "domain/services/suggest_service.h"

#include "domain/models/search_index_models.h"
#include "infrastructure/data/conclusion_index_repository.h"

#include <QHash>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace domain::services {
namespace {

using domain::models::FieldMaskLegend;
using domain::models::SuggestOptions;
using domain::models::SuggestionItem;
using domain::models::SuggestionResult;

QString collapseWhitespace(const QString& input)
{
    QString output;
    output.reserve(input.size());

    bool lastWasSpace = false;
    for (QChar ch : input) {
        if (ch.isSpace()) {
            if (!lastWasSpace) {
                output.push_back(QChar::Space);
            }
            lastWasSpace = true;
            continue;
        }
        output.push_back(ch);
        lastWasSpace = false;
    }
    return output.trimmed();
}

QSet<QString> toLowerSet(const QStringList& values)
{
    QSet<QString> set;
    set.reserve(values.size());
    for (const QString& value : values) {
        const QString normalized = value.trimmed().toLower();
        if (!normalized.isEmpty()) {
            set.insert(normalized);
        }
    }
    return set;
}

bool matchesOptionalFilter(const QString& value, const QSet<QString>& filter)
{
    if (filter.isEmpty()) {
        return true;
    }
    return filter.contains(value.trimmed().toLower());
}

bool matchesOptionalTagFilter(const QStringList& tags, const QSet<QString>& tagFilter)
{
    if (tagFilter.isEmpty()) {
        return true;
    }

    for (const QString& tag : tags) {
        if (tagFilter.contains(tag.trimmed().toLower())) {
            return true;
        }
    }
    return false;
}

bool matchesAnyPrefix(const QString& text, const QStringList& queryKeys)
{
    for (const QString& key : queryKeys) {
        if (text.startsWith(key, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

bool matchesAnyRelatedTerm(const QString& text, const QStringList& queryKeys)
{
    for (const QString& key : queryKeys) {
        if (text.startsWith(key, Qt::CaseInsensitive) || text.contains(key, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

struct PostingSignal {
    bool hasAnyDoc = false;
    double scoreSignal = 0.0;
    double avgDocBoost = 0.0;
    quint32 mergedFieldMask = 0;
    QStringList targetDocIds;
};

PostingSignal collectPostingSignal(const QVector<domain::models::PostingEntry>& postings,
                                   const infrastructure::data::ConclusionIndexRepository& repository,
                                   const QSet<QString>& moduleFilter,
                                   const QSet<QString>& categoryFilter,
                                   const QSet<QString>& tagFilter)
{
    PostingSignal signal;
    QSet<QString> dedupeDocIds;
    dedupeDocIds.reserve(postings.size());

    double sumDocBoost = 0.0;
    int acceptedCount = 0;
    for (const domain::models::PostingEntry& posting : postings) {
        const domain::models::IndexDocRecord* doc = repository.getDocById(posting.docId);
        if (doc == nullptr) {
            continue;
        }
        if (!matchesOptionalFilter(doc->module, moduleFilter)
            || !matchesOptionalFilter(doc->category, categoryFilter)
            || !matchesOptionalTagFilter(doc->tags, tagFilter)) {
            continue;
        }

        signal.hasAnyDoc = true;
        signal.scoreSignal += posting.score;
        signal.mergedFieldMask |= posting.fieldMask;
        sumDocBoost += doc->searchBoost;
        ++acceptedCount;

        if (!dedupeDocIds.contains(posting.docId)) {
            dedupeDocIds.insert(posting.docId);
            if (signal.targetDocIds.size() < 12) {
                signal.targetDocIds.push_back(posting.docId);
            }
        }
    }

    if (acceptedCount > 0) {
        signal.scoreSignal /= acceptedCount;
        signal.avgDocBoost = sumDocBoost / acceptedCount;
    }
    return signal;
}

double fieldQualityScore(quint32 fieldMask, const FieldMaskLegend& legend)
{
    const auto bit = [&legend](const QString& name) { return legend.value(name, 0U); };

    double score = 0.0;
    if ((fieldMask & bit(QStringLiteral("query_template"))) != 0U) {
        score += 12.0;
    }
    if ((fieldMask & bit(QStringLiteral("title"))) != 0U) {
        score += 11.0;
    }
    if ((fieldMask & bit(QStringLiteral("alias"))) != 0U) {
        score += 10.0;
    }
    if ((fieldMask & bit(QStringLiteral("keyword"))) != 0U) {
        score += 8.0;
    }
    if ((fieldMask & bit(QStringLiteral("tag"))) != 0U) {
        score += 7.0;
    }
    if ((fieldMask & bit(QStringLiteral("formula"))) != 0U) {
        score += 8.0;
    }
    if ((fieldMask & bit(QStringLiteral("pinyin"))) != 0U) {
        score += 6.0;
    }
    if ((fieldMask & bit(QStringLiteral("pinyin_abbr"))) != 0U) {
        score += 5.0;
    }
    return score;
}

int fieldQualityTier(quint32 fieldMask, const FieldMaskLegend& legend)
{
    const auto bit = [&legend](const QString& name) { return legend.value(name, 0U); };
    if (((fieldMask & bit(QStringLiteral("query_template"))) != 0U)
        || ((fieldMask & bit(QStringLiteral("title"))) != 0U)
        || ((fieldMask & bit(QStringLiteral("alias"))) != 0U)) {
        return 3;
    }
    if (((fieldMask & bit(QStringLiteral("keyword"))) != 0U) || ((fieldMask & bit(QStringLiteral("tag"))) != 0U)) {
        return 2;
    }
    if (((fieldMask & bit(QStringLiteral("pinyin"))) != 0U)
        || ((fieldMask & bit(QStringLiteral("pinyin_abbr"))) != 0U)) {
        return 1;
    }
    return 0;
}

double prefixClosenessScore(const QString& candidateText, const QString& queryText)
{
    if (candidateText.compare(queryText, Qt::CaseInsensitive) == 0) {
        return 40.0;
    }
    if (candidateText.startsWith(queryText, Qt::CaseInsensitive)) {
        const int extraLength = candidateText.size() - queryText.size();
        return 32.0 - std::min(18.0, extraLength * 0.85);
    }
    if (candidateText.contains(queryText, Qt::CaseInsensitive)) {
        return 10.0;
    }
    return 0.0;
}

double lengthReasonablenessScore(const QString& candidateText, const QString& queryText)
{
    const int delta = candidateText.size() - queryText.size();
    if (delta >= 0 && delta <= 8) {
        return 6.0;
    }
    if (delta > 8 && delta <= 20) {
        return 2.0;
    }
    if (delta < 0) {
        return -6.0;
    }
    return -10.0;
}

struct ScoredSuggestion {
    SuggestionItem item;
    int qualityTier = 0;
};

}  // namespace

SuggestService::SuggestService(const infrastructure::data::ConclusionIndexRepository* repository) : repository_(repository)
{
}

void SuggestService::setRepository(const infrastructure::data::ConclusionIndexRepository* repository)
{
    repository_ = repository;
}

const infrastructure::data::ConclusionIndexRepository* SuggestService::repository() const
{
    return repository_;
}

SuggestionResult SuggestService::suggest(const QString& query, const SuggestOptions& options) const
{
    SuggestionResult result;
    result.query = query;
    result.normalizedQuery = domain::models::normalizeQueryText(query);

    const QString compactRawQuery = collapseWhitespace(query);
    if (repository_ == nullptr || compactRawQuery.isEmpty()) {
        return result;
    }

    const int maxResults = options.maxResults > 0 ? options.maxResults : 8;
    QStringList queryKeys;
    queryKeys.push_back(compactRawQuery);
    if (result.normalizedQuery != compactRawQuery) {
        queryKeys.push_back(result.normalizedQuery);
    }

    const QSet<QString> moduleFilter = toLowerSet(options.moduleFilter);
    const QSet<QString> categoryFilter = toLowerSet(options.categoryFilter);
    const QSet<QString> tagFilter = toLowerSet(options.tagFilter);
    const FieldMaskLegend& legend = repository_->fieldMaskLegend();
    const QString scoringQuery = result.normalizedQuery.isEmpty() ? compactRawQuery : result.normalizedQuery;

    QHash<QString, ScoredSuggestion> dedupedByNormalizedText;
    QVector<ScoredSuggestion> undeduped;
    undeduped.reserve(64);

    const auto acceptSuggestion = [&](ScoredSuggestion candidate) {
        if (candidate.item.normalizedText.isEmpty()) {
            return;
        }

        if (!options.enableExactDedup) {
            undeduped.push_back(std::move(candidate));
            return;
        }

        const QString dedupeKey = candidate.item.normalizedText;
        const auto existing = dedupedByNormalizedText.constFind(dedupeKey);
        if (existing == dedupedByNormalizedText.constEnd()) {
            dedupedByNormalizedText.insert(dedupeKey, std::move(candidate));
            return;
        }

        const double scoreDelta = candidate.item.score - existing->item.score;
        if (scoreDelta > 1e-9) {
            dedupedByNormalizedText[dedupeKey] = std::move(candidate);
            return;
        }
        if (std::fabs(scoreDelta) <= 1e-9 && candidate.qualityTier > existing->qualityTier) {
            dedupedByNormalizedText[dedupeKey] = std::move(candidate);
        }
    };

    if (options.enablePrefix) {
        repository_->forEachPrefixEntry([&](const QString& key, const QVector<domain::models::PostingEntry>& postings) {
            if (!matchesAnyPrefix(key, queryKeys)) {
                return;
            }

            const PostingSignal signal =
                collectPostingSignal(postings, *repository_, moduleFilter, categoryFilter, tagFilter);
            if (!signal.hasAnyDoc) {
                return;
            }

            ScoredSuggestion candidate;
            candidate.item.text = key;
            candidate.item.normalizedText = domain::models::normalizeQueryText(key);
            candidate.item.source = QStringLiteral("prefix_index");
            candidate.item.matchedFields = domain::models::decodeFieldMask(signal.mergedFieldMask, legend);
            candidate.item.targetDocIds = signal.targetDocIds;
            candidate.qualityTier = fieldQualityTier(signal.mergedFieldMask, legend);

            double score = 0.0;
            score += prefixClosenessScore(candidate.item.normalizedText, scoringQuery);
            score += lengthReasonablenessScore(candidate.item.normalizedText, scoringQuery);
            score += fieldQualityScore(signal.mergedFieldMask, legend);
            score += signal.scoreSignal * 0.55;
            score += signal.avgDocBoost * 4.0;
            score += 7.0;  // prefix source bonus
            candidate.item.score = score;

            if (options.enableDebug) {
                candidate.item.debugInfo.insert(QStringLiteral("source"), candidate.item.source);
                candidate.item.debugInfo.insert(QStringLiteral("score_signal"), signal.scoreSignal);
                candidate.item.debugInfo.insert(QStringLiteral("avg_doc_boost"), signal.avgDocBoost);
                candidate.item.debugInfo.insert(QStringLiteral("merged_field_mask"), static_cast<qint64>(signal.mergedFieldMask));
            }

            acceptSuggestion(std::move(candidate));
        });
    }

    int termSupplementCount = 0;
    repository_->forEachTermEntry([&](const QString& key, const QVector<domain::models::PostingEntry>& postings) {
        if (termSupplementCount >= maxResults * 8) {
            return;
        }
        if (!matchesAnyRelatedTerm(key, queryKeys)) {
            return;
        }

        const PostingSignal signal = collectPostingSignal(postings, *repository_, moduleFilter, categoryFilter, tagFilter);
        if (!signal.hasAnyDoc) {
            return;
        }

        ScoredSuggestion candidate;
        candidate.item.text = key;
        candidate.item.normalizedText = domain::models::normalizeQueryText(key);
        candidate.item.source = QStringLiteral("term_index");
        candidate.item.matchedFields = domain::models::decodeFieldMask(signal.mergedFieldMask, legend);
        candidate.item.targetDocIds = signal.targetDocIds;
        candidate.qualityTier = fieldQualityTier(signal.mergedFieldMask, legend);

        double score = 0.0;
        score += prefixClosenessScore(candidate.item.normalizedText, scoringQuery);
        score += lengthReasonablenessScore(candidate.item.normalizedText, scoringQuery);
        score += fieldQualityScore(signal.mergedFieldMask, legend) * 0.85;
        score += signal.scoreSignal * 0.45;
        score += signal.avgDocBoost * 3.0;
        score += 3.0;  // term supplement bonus
        candidate.item.score = score;

        if (options.enableDebug) {
            candidate.item.debugInfo.insert(QStringLiteral("source"), candidate.item.source);
            candidate.item.debugInfo.insert(QStringLiteral("score_signal"), signal.scoreSignal);
            candidate.item.debugInfo.insert(QStringLiteral("avg_doc_boost"), signal.avgDocBoost);
            candidate.item.debugInfo.insert(QStringLiteral("merged_field_mask"), static_cast<qint64>(signal.mergedFieldMask));
        }

        acceptSuggestion(std::move(candidate));
        ++termSupplementCount;
    });

    QVector<SuggestionItem> items;
    if (options.enableExactDedup) {
        items.reserve(dedupedByNormalizedText.size());
        for (auto it = dedupedByNormalizedText.constBegin(); it != dedupedByNormalizedText.constEnd(); ++it) {
            items.push_back(it.value().item);
        }
    } else {
        items.reserve(undeduped.size());
        for (const ScoredSuggestion& scored : undeduped) {
            items.push_back(scored.item);
        }
    }

    std::sort(items.begin(), items.end(), [](const SuggestionItem& lhs, const SuggestionItem& rhs) {
        if (std::fabs(lhs.score - rhs.score) > 1e-9) {
            return lhs.score > rhs.score;
        }
        const int textCompare = lhs.text.compare(rhs.text, Qt::CaseInsensitive);
        if (textCompare != 0) {
            return textCompare < 0;
        }
        return lhs.source < rhs.source;
    });

    result.total = items.size();
    if (items.size() > maxResults) {
        items.resize(maxResults);
    }
    result.items = std::move(items);
    return result;
}

}  // namespace domain::services
