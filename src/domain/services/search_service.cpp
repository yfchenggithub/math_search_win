#include "domain/services/search_service.h"

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
using domain::models::IndexDocRecord;
using domain::models::SearchHit;
using domain::models::SearchOptions;
using domain::models::SearchResult;

// 把一个 QString 里的连续空白字符压缩成一个普通空格，并去掉首尾空白
// "  hello   world \n\t Qt  "--> "hello world Qt"
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

double fieldMaskWeight(quint32 fieldMask, const FieldMaskLegend& legend)
{
    const auto bit = [&legend](const QString& name) { return legend.value(name, 0U); };

    double weight = 1.0;
    if ((fieldMask & bit(QStringLiteral("title"))) != 0U) {
        weight += 0.60;
    }
    if ((fieldMask & bit(QStringLiteral("alias"))) != 0U) {
        weight += 0.45;
    }
    if ((fieldMask & bit(QStringLiteral("query_template"))) != 0U) {
        weight += 0.45;
    }
    if ((fieldMask & bit(QStringLiteral("formula"))) != 0U) {
        weight += 0.35;
    }
    if ((fieldMask & bit(QStringLiteral("formula_token"))) != 0U) {
        weight += 0.20;
    }
    if ((fieldMask & bit(QStringLiteral("pinyin"))) != 0U) {
        weight += 0.25;
    }
    if ((fieldMask & bit(QStringLiteral("pinyin_abbr"))) != 0U) {
        weight += 0.18;
    }
    if ((fieldMask & bit(QStringLiteral("keyword"))) != 0U) {
        weight += 0.22;
    }
    if ((fieldMask & bit(QStringLiteral("tag"))) != 0U) {
        weight += 0.16;
    }
    if ((fieldMask & bit(QStringLiteral("summary"))) != 0U) {
        weight += 0.08;
    }
    return weight;
}

struct ScoreAccumulator {
    double postingScore = 0.0;
    quint32 mergedFieldMask = 0;
    int termHits = 0;
    int prefixHits = 0;
    bool exactTermMatch = false;
};

void applyPostingBatch(const QVector<domain::models::PostingEntry>& postings,
                       bool fromTerm,
                       const QSet<QString>& moduleFilter,
                       const QSet<QString>& categoryFilter,
                       const QSet<QString>& tagFilter,
                       const infrastructure::data::ConclusionIndexRepository& repository,
                       QHash<QString, ScoreAccumulator>* accumulators)
{
    if (accumulators == nullptr) {
        return;
    }

    const double sourceWeight = fromTerm ? 1.0 : 0.68;
    const FieldMaskLegend& legend = repository.fieldMaskLegend();

    for (const domain::models::PostingEntry& posting : postings) {
        const IndexDocRecord* doc = repository.getDocById(posting.docId);
        if (doc == nullptr) {
            continue;
        }
        if (!matchesOptionalFilter(doc->module, moduleFilter)
            || !matchesOptionalFilter(doc->category, categoryFilter)
            || !matchesOptionalTagFilter(doc->tags, tagFilter)) {
            continue;
        }

        ScoreAccumulator& accumulator = (*accumulators)[posting.docId];
        accumulator.postingScore += posting.score * sourceWeight * fieldMaskWeight(posting.fieldMask, legend);
        accumulator.mergedFieldMask |= posting.fieldMask;
        if (fromTerm) {
            ++accumulator.termHits;
            accumulator.exactTermMatch = true;
        } else {
            ++accumulator.prefixHits;
        }
    }
}

}  // namespace

SearchService::SearchService(const infrastructure::data::ConclusionIndexRepository* repository) : repository_(repository)
{
}

void SearchService::setRepository(const infrastructure::data::ConclusionIndexRepository* repository)
{
    repository_ = repository;
}

const infrastructure::data::ConclusionIndexRepository* SearchService::repository() const
{
    return repository_;
}

SearchResult SearchService::search(const QString& query, const SearchOptions& options) const
{
    SearchResult result;
    result.query = query;
    result.normalizedQuery = domain::models::normalizeQueryText(query);

    const QString compactRawQuery = collapseWhitespace(query);
    if (repository_ == nullptr || compactRawQuery.isEmpty()) {
        return result;
    }

    const int maxResults = options.maxResults > 0 ? options.maxResults : 20;
    const bool allowPrefix = options.enablePrefix && result.normalizedQuery.size() >= 2;

    QStringList queryKeys;
    queryKeys.push_back(compactRawQuery);
    if (result.normalizedQuery != compactRawQuery) {
        queryKeys.push_back(result.normalizedQuery);
    }

    const QSet<QString> moduleFilter = toLowerSet(options.moduleFilter);
    const QSet<QString> categoryFilter = toLowerSet(options.categoryFilter);
    const QSet<QString> tagFilter = toLowerSet(options.tagFilter);

    QHash<QString, ScoreAccumulator> accumulators;
    for (const QString& key : queryKeys) {
        if (options.enableTerm) {
            const QVector<domain::models::PostingEntry>* termPostings = repository_->findTerm(key);
            if (termPostings != nullptr) {
                applyPostingBatch(*termPostings,
                                  true,
                                  moduleFilter,
                                  categoryFilter,
                                  tagFilter,
                                  *repository_,
                                  &accumulators);
            }
        }

        if (allowPrefix) {
            const QVector<domain::models::PostingEntry>* prefixPostings = repository_->findPrefix(key);
            if (prefixPostings != nullptr) {
                applyPostingBatch(*prefixPostings,
                                  false,
                                  moduleFilter,
                                  categoryFilter,
                                  tagFilter,
                                  *repository_,
                                  &accumulators);
            }
        }
    }

    QVector<SearchHit> hits;
    hits.reserve(accumulators.size());
    for (auto it = accumulators.constBegin(); it != accumulators.constEnd(); ++it) {
        const IndexDocRecord* doc = repository_->getDocById(it.key());
        if (doc == nullptr) {
            continue;
        }

        const ScoreAccumulator& score = it.value();
        double finalScore = score.postingScore;
        finalScore += doc->rank * 0.03;
        finalScore += doc->searchBoost * 8.0;
        if (score.termHits > 0) {
            finalScore += 2.0;
        }
        if (options.enableExactBoost && score.exactTermMatch) {
            finalScore += 25.0;
        }

        SearchHit hit;
        hit.docId = doc->id;
        hit.title = doc->title;
        hit.module = doc->module;
        hit.category = doc->category;
        hit.difficulty = doc->difficulty;
        hit.tags = doc->tags;
        hit.summary = doc->summary;
        hit.coreFormula = doc->coreFormula;
        hit.score = finalScore;
        hit.matchedFields = domain::models::decodeFieldMask(score.mergedFieldMask, repository_->fieldMaskLegend());
        if (options.enableDebug) {
            hit.debugInfo.insert(QStringLiteral("term_hits"), score.termHits);
            hit.debugInfo.insert(QStringLiteral("prefix_hits"), score.prefixHits);
            hit.debugInfo.insert(QStringLiteral("posting_score"), score.postingScore);
            hit.debugInfo.insert(QStringLiteral("merged_field_mask"), static_cast<qint64>(score.mergedFieldMask));
        }

        hits.push_back(std::move(hit));
    }

    std::sort(hits.begin(), hits.end(), [](const SearchHit& lhs, const SearchHit& rhs) {
        if (std::fabs(lhs.score - rhs.score) > 1e-9) {
            return lhs.score > rhs.score;
        }
        const int titleCompare = lhs.title.compare(rhs.title, Qt::CaseInsensitive);
        if (titleCompare != 0) {
            return titleCompare < 0;
        }
        return lhs.docId < rhs.docId;
    });

    result.total = hits.size();
    if (hits.size() > maxResults) {
        hits.resize(maxResults);
    }
    result.hits = std::move(hits);
    return result;
}

}  // namespace domain::services
