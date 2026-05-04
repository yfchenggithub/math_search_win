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
using domain::models::DomainEntry;
using domain::models::TopicEntry;
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

bool hasTrailingUnclosedBrackets(const QString& text)
{
    int roundBracketDepth = 0;      // ()
    int fullRoundBracketDepth = 0;  // （）
    int squareBracketDepth = 0;     // []
    int fullSquareBracketDepth = 0; // 【】
    int curlyBracketDepth = 0;      // {}

    for (QChar ch : text) {
        switch (ch.unicode()) {
        case '(':
            ++roundBracketDepth;
            break;
        case ')':
            if (roundBracketDepth > 0) {
                --roundBracketDepth;
            }
            break;
        case 0xFF08:  // （
            ++fullRoundBracketDepth;
            break;
        case 0xFF09:  // ）
            if (fullRoundBracketDepth > 0) {
                --fullRoundBracketDepth;
            }
            break;
        case '[':
            ++squareBracketDepth;
            break;
        case ']':
            if (squareBracketDepth > 0) {
                --squareBracketDepth;
            }
            break;
        case 0x3010:  // 【
            ++fullSquareBracketDepth;
            break;
        case 0x3011:  // 】
            if (fullSquareBracketDepth > 0) {
                --fullSquareBracketDepth;
            }
            break;
        case '{':
            ++curlyBracketDepth;
            break;
        case '}':
            if (curlyBracketDepth > 0) {
                --curlyBracketDepth;
            }
            break;
        default:
            break;
        }
    }

    return roundBracketDepth > 0 || fullRoundBracketDepth > 0 || squareBracketDepth > 0
        || fullSquareBracketDepth > 0 || curlyBracketDepth > 0;
}

bool isLowQualitySuggestionText(const QString& text)
{
    const QString normalized = collapseWhitespace(text);
    if (normalized.isEmpty()) {
        return true;
    }
    return hasTrailingUnclosedBrackets(normalized);
}

bool containsCjkCharacter(const QString& text)
{
    for (QChar ch : text) {
        const uint codepoint = ch.unicode();
        if ((codepoint >= 0x3400U && codepoint <= 0x4DBFU) || (codepoint >= 0x4E00U && codepoint <= 0x9FFFU)
            || (codepoint >= 0xF900U && codepoint <= 0xFAFFU)) {
            return true;
        }
    }
    return false;
}

bool matchesDocFilters(const domain::models::IndexDocRecord& doc,
                       const QSet<QString>& moduleFilter,
                       const QSet<QString>& categoryFilter,
                       const QSet<QString>& tagFilter)
{
    return matchesOptionalFilter(doc.module, moduleFilter) && matchesOptionalFilter(doc.category, categoryFilter)
        && matchesOptionalTagFilter(doc.tags, tagFilter);
}

struct PostingSignal {
    bool hasAnyDoc = false;
    double scoreSignal = 0.0;
    double avgDocBoost = 0.0;
    quint32 mergedFieldMask = 0;
    QStringList targetDocIds;
};

struct PrefixCandidateEntry {
    QString key;
    QString normalizedKey;
    PostingSignal signal;
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

bool hasAnyOverlappedDocId(const QStringList& lhsDocIds, const QStringList& rhsDocIds)
{
    if (lhsDocIds.isEmpty() || rhsDocIds.isEmpty()) {
        return false;
    }

    if (lhsDocIds.size() <= rhsDocIds.size()) {
        QSet<QString> lhsSet;
        lhsSet.reserve(lhsDocIds.size());
        for (const QString& docId : lhsDocIds) {
            lhsSet.insert(docId);
        }
        for (const QString& docId : rhsDocIds) {
            if (lhsSet.contains(docId)) {
                return true;
            }
        }
        return false;
    }

    QSet<QString> rhsSet;
    rhsSet.reserve(rhsDocIds.size());
    for (const QString& docId : rhsDocIds) {
        rhsSet.insert(docId);
    }
    for (const QString& docId : lhsDocIds) {
        if (rhsSet.contains(docId)) {
            return true;
        }
    }
    return false;
}

bool isSemanticTruncatedPrefixCandidate(const PrefixCandidateEntry& candidate,
                                        const QVector<PrefixCandidateEntry>& allCandidates,
                                        const infrastructure::data::ConclusionIndexRepository& repository)
{
    if (candidate.normalizedKey.isEmpty()) {
        return true;
    }
    if (!containsCjkCharacter(candidate.key)) {
        return false;
    }
    if (repository.findTerm(candidate.normalizedKey) != nullptr) {
        return false;
    }

    for (const PrefixCandidateEntry& other : allCandidates) {
        if (other.normalizedKey.size() <= candidate.normalizedKey.size()) {
            continue;
        }
        if (!other.normalizedKey.startsWith(candidate.normalizedKey, Qt::CaseInsensitive)) {
            continue;
        }
        if (!hasAnyOverlappedDocId(candidate.signal.targetDocIds, other.signal.targetDocIds)) {
            continue;
        }
        return true;
    }
    return false;
}

struct SuggestionSeedSignal {
    bool accepted = false;
    double avgDocBoost = 0.0;
    quint32 mergedFieldMask = 0;
    QStringList targetDocIds;
};

SuggestionSeedSignal collectSuggestionSeedSignal(const domain::models::IndexedSuggestionSeed& seed,
                                                 const infrastructure::data::ConclusionIndexRepository& repository,
                                                 const QSet<QString>& moduleFilter,
                                                 const QSet<QString>& categoryFilter,
                                                 const QSet<QString>& tagFilter)
{
    SuggestionSeedSignal signal;
    const QString seedDocId = seed.docId.trimmed();
    if (seedDocId.isEmpty()) {
        // Suggestions without docId cannot be checked against module/category/tag filters.
        if (!moduleFilter.isEmpty() || !categoryFilter.isEmpty() || !tagFilter.isEmpty()) {
            return signal;
        }
        signal.accepted = true;
        return signal;
    }

    const domain::models::IndexDocRecord* doc = repository.getDocById(seedDocId);
    if (doc == nullptr) {
        return signal;
    }
    if (!matchesOptionalFilter(doc->module, moduleFilter)
        || !matchesOptionalFilter(doc->category, categoryFilter)
        || !matchesOptionalTagFilter(doc->tags, tagFilter)) {
        return signal;
    }

    signal.accepted = true;
    signal.avgDocBoost = doc->searchBoost;
    signal.targetDocIds.push_back(doc->id);
    return signal;
}

struct DomainTopicSignal {
    bool hasAnyDoc = false;
    int matchedDocCount = 0;
    double avgDocBoost = 0.0;
    QStringList targetDocIds;
};

DomainTopicSignal collectDomainTopicSignal(const QStringList& docIds,
                                           const infrastructure::data::ConclusionIndexRepository& repository,
                                           const QSet<QString>& moduleFilter,
                                           const QSet<QString>& categoryFilter,
                                           const QSet<QString>& tagFilter)
{
    DomainTopicSignal signal;
    if (docIds.isEmpty()) {
        signal.hasAnyDoc = moduleFilter.isEmpty() && categoryFilter.isEmpty() && tagFilter.isEmpty();
        return signal;
    }

    QSet<QString> dedupedDocIds;
    dedupedDocIds.reserve(docIds.size());
    double sumDocBoost = 0.0;
    for (const QString& rawDocId : docIds) {
        const QString docId = rawDocId.trimmed();
        if (docId.isEmpty() || dedupedDocIds.contains(docId)) {
            continue;
        }
        dedupedDocIds.insert(docId);

        const domain::models::IndexDocRecord* doc = repository.getDocById(docId);
        if (doc == nullptr || !matchesDocFilters(*doc, moduleFilter, categoryFilter, tagFilter)) {
            continue;
        }

        signal.hasAnyDoc = true;
        ++signal.matchedDocCount;
        sumDocBoost += doc->searchBoost;
        if (signal.targetDocIds.size() < 12) {
            signal.targetDocIds.push_back(doc->id);
        }
    }

    if (signal.matchedDocCount > 0) {
        signal.avgDocBoost = sumDocBoost / signal.matchedDocCount;
    }
    return signal;
}

bool isDomainMatchedByQuery(const DomainEntry& domain, const QStringList& queryKeys)
{
    QStringList domainTokens = domain.aliases;
    domainTokens.push_back(domain.name);
    for (const QString& token : domainTokens) {
        const QString normalizedToken = collapseWhitespace(token);
        if (normalizedToken.isEmpty()) {
            continue;
        }
        if (matchesAnyRelatedTerm(normalizedToken, queryKeys)) {
            return true;
        }
    }
    return false;
}

bool isTopicMatchedByQuery(const TopicEntry& topic, const QStringList& queryKeys)
{
    QStringList topicTokens = topic.aliases;
    topicTokens.push_back(topic.name);
    for (const QString& token : topicTokens) {
        const QString normalizedToken = collapseWhitespace(token);
        if (normalizedToken.isEmpty()) {
            continue;
        }
        if (matchesAnyRelatedTerm(normalizedToken, queryKeys)) {
            return true;
        }
    }
    return false;
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
    const QString requiredPrefix = result.normalizedQuery;
    const QVector<domain::models::IndexedSuggestionSeed>& indexedSuggestions = repository_->optionalSuggestions();

    QHash<QString, ScoredSuggestion> dedupedByNormalizedText;
    QVector<ScoredSuggestion> undeduped;
    undeduped.reserve(64);

    const auto acceptSuggestion = [&](ScoredSuggestion candidate, bool bypassPrefixConstraint = false) {
        if (candidate.item.normalizedText.isEmpty()) {
            return;
        }
        if (!bypassPrefixConstraint && !candidate.item.normalizedText.startsWith(requiredPrefix, Qt::CaseInsensitive)) {
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

    const auto candidateCount = [&]() {
        return options.enableExactDedup ? dedupedByNormalizedText.size() : undeduped.size();
    };

    if (options.enableDomainTopicExpansion && repository_->hasDomainTopicMap()) {
        const auto& domainTopicMap = repository_->domainTopicMap();
        struct DomainTopicCandidateState {
            int domainIndex = -1;
            int topicIndex = -1;
            bool matchedByDomain = false;
            bool directTopicHit = false;
        };

        QHash<QString, DomainTopicCandidateState> candidateTopicsByKey;
        for (int domainIndex = 0; domainIndex < domainTopicMap.domains.size(); ++domainIndex) {
            const DomainEntry& domain = domainTopicMap.domains.at(domainIndex);
            const bool matchedDomain = isDomainMatchedByQuery(domain, queryKeys);
            for (int topicIndex = 0; topicIndex < domain.topics.size(); ++topicIndex) {
                const TopicEntry& topic = domain.topics.at(topicIndex);
                const bool directTopicHit = isTopicMatchedByQuery(topic, queryKeys);
                if (!matchedDomain && !directTopicHit) {
                    continue;
                }

                const QString topicText = collapseWhitespace(topic.name);
                if (topicText.isEmpty()) {
                    continue;
                }
                const QString topicKey = domain::models::normalizeQueryText(topicText);
                if (topicKey.isEmpty()) {
                    continue;
                }

                auto existing = candidateTopicsByKey.find(topicKey);
                if (existing == candidateTopicsByKey.end()) {
                    DomainTopicCandidateState state;
                    state.domainIndex = domainIndex;
                    state.topicIndex = topicIndex;
                    state.matchedByDomain = matchedDomain;
                    state.directTopicHit = directTopicHit;
                    candidateTopicsByKey.insert(topicKey, state);
                    continue;
                }

                existing->matchedByDomain = existing->matchedByDomain || matchedDomain;
                existing->directTopicHit = existing->directTopicHit || directTopicHit;
            }
        }

        for (auto it = candidateTopicsByKey.constBegin(); it != candidateTopicsByKey.constEnd(); ++it) {
            const DomainTopicCandidateState& state = it.value();
            if (state.domainIndex < 0 || state.domainIndex >= domainTopicMap.domains.size()) {
                continue;
            }

            const DomainEntry& domain = domainTopicMap.domains.at(state.domainIndex);
            if (state.topicIndex < 0 || state.topicIndex >= domain.topics.size()) {
                continue;
            }
            const TopicEntry& topic = domain.topics.at(state.topicIndex);

            const QString topicText = collapseWhitespace(topic.name);
            if (topicText.isEmpty()) {
                continue;
            }

            const DomainTopicSignal signal =
                collectDomainTopicSignal(topic.docIds, *repository_, moduleFilter, categoryFilter, tagFilter);
            if (!signal.hasAnyDoc) {
                continue;
            }

            ScoredSuggestion candidate;
            candidate.item.text = topicText;
            candidate.item.normalizedText = domain::models::normalizeQueryText(topicText);
            candidate.item.source = QStringLiteral("domain_topic");
            candidate.item.suggestKind = QStringLiteral("domain_topic");
            candidate.item.domainName = domain.name;
            candidate.item.isDomainEntry = false;
            candidate.item.matchedFields = {QStringLiteral("domain"), QStringLiteral("topic")};
            candidate.item.targetDocIds = signal.targetDocIds;
            candidate.qualityTier = 4;

            double score = 0.0;
            score += prefixClosenessScore(candidate.item.normalizedText, scoringQuery);
            score += lengthReasonablenessScore(candidate.item.normalizedText, scoringQuery);
            score += signal.avgDocBoost * 3.8;
            score += std::min(8.0, static_cast<double>(signal.matchedDocCount) * 1.2);
            score += 10.0;  // domain/topic source bonus
            if (state.matchedByDomain) {
                score += 24.0;
            }
            if (state.directTopicHit) {
                score += 18.0;
            }
            candidate.item.score = score;

            if (options.enableDebug) {
                candidate.item.debugInfo.insert(QStringLiteral("source"), candidate.item.source);
                candidate.item.debugInfo.insert(QStringLiteral("domain_name"), domain.name);
                candidate.item.debugInfo.insert(QStringLiteral("matched_by_domain"), state.matchedByDomain);
                candidate.item.debugInfo.insert(QStringLiteral("direct_topic_hit"), state.directTopicHit);
                candidate.item.debugInfo.insert(QStringLiteral("matched_doc_count"), signal.matchedDocCount);
                candidate.item.debugInfo.insert(QStringLiteral("avg_doc_boost"), signal.avgDocBoost);
            }

            acceptSuggestion(std::move(candidate), true);
        }
    }

    for (const domain::models::IndexedSuggestionSeed& seed : indexedSuggestions) {
        const QString seedText = collapseWhitespace(seed.text);
        if (seedText.isEmpty()) {
            continue;
        }
        if (!matchesAnyPrefix(seedText, queryKeys)) {
            continue;
        }

        const SuggestionSeedSignal seedSignal =
            collectSuggestionSeedSignal(seed, *repository_, moduleFilter, categoryFilter, tagFilter);
        if (!seedSignal.accepted) {
            continue;
        }

        ScoredSuggestion candidate;
        candidate.item.text = seedText;
        candidate.item.normalizedText = domain::models::normalizeQueryText(seedText);
        candidate.item.source = QStringLiteral("indexed_suggestion");
        candidate.item.suggestKind = QStringLiteral("indexed");
        candidate.item.matchedFields = domain::models::decodeFieldMask(seedSignal.mergedFieldMask, legend);
        candidate.item.targetDocIds = seedSignal.targetDocIds;
        candidate.qualityTier = fieldQualityTier(seedSignal.mergedFieldMask, legend);

        double score = 0.0;
        score += prefixClosenessScore(candidate.item.normalizedText, scoringQuery);
        score += lengthReasonablenessScore(candidate.item.normalizedText, scoringQuery);
        score += seed.score * 0.08;
        score += seedSignal.avgDocBoost * 4.5;
        score += 8.0;  // indexed suggestion source bonus
        candidate.item.score = score;

        if (options.enableDebug) {
            candidate.item.debugInfo.insert(QStringLiteral("source"), candidate.item.source);
            candidate.item.debugInfo.insert(QStringLiteral("seed_score"), seed.score);
            candidate.item.debugInfo.insert(QStringLiteral("avg_doc_boost"), seedSignal.avgDocBoost);
        }

        acceptSuggestion(std::move(candidate));
    }

    const bool skipExpensiveIndexScan = options.enablePrefix && !indexedSuggestions.isEmpty() && candidateCount() >= maxResults;

    if (options.enablePrefix && !skipExpensiveIndexScan) {
        QVector<PrefixCandidateEntry> prefixCandidates;
        prefixCandidates.reserve(maxResults * 6);
        repository_->forEachPrefixEntry([&](const QString& key, const QVector<domain::models::PostingEntry>& postings) {
            if (!matchesAnyPrefix(key, queryKeys)) {
                return;
            }
            if (isLowQualitySuggestionText(key)) {
                return;
            }

            const PostingSignal signal =
                collectPostingSignal(postings, *repository_, moduleFilter, categoryFilter, tagFilter);
            if (!signal.hasAnyDoc) {
                return;
            }

            PrefixCandidateEntry row;
            row.key = key;
            row.normalizedKey = domain::models::normalizeQueryText(key);
            row.signal = signal;
            prefixCandidates.push_back(std::move(row));
        });

        for (const PrefixCandidateEntry& prefixRow : prefixCandidates) {
            if (isSemanticTruncatedPrefixCandidate(prefixRow, prefixCandidates, *repository_)) {
                continue;
            }

            ScoredSuggestion candidate;
            candidate.item.text = prefixRow.key;
            candidate.item.normalizedText = prefixRow.normalizedKey;
            candidate.item.source = QStringLiteral("prefix_index");
            candidate.item.suggestKind = QStringLiteral("prefix");
            candidate.item.matchedFields = domain::models::decodeFieldMask(prefixRow.signal.mergedFieldMask, legend);
            candidate.item.targetDocIds = prefixRow.signal.targetDocIds;
            candidate.qualityTier = fieldQualityTier(prefixRow.signal.mergedFieldMask, legend);

            double score = 0.0;
            score += prefixClosenessScore(candidate.item.normalizedText, scoringQuery);
            score += lengthReasonablenessScore(candidate.item.normalizedText, scoringQuery);
            score += fieldQualityScore(prefixRow.signal.mergedFieldMask, legend);
            score += prefixRow.signal.scoreSignal * 0.55;
            score += prefixRow.signal.avgDocBoost * 4.0;
            score += 7.0;  // prefix source bonus
            candidate.item.score = score;

            if (options.enableDebug) {
                candidate.item.debugInfo.insert(QStringLiteral("source"), candidate.item.source);
                candidate.item.debugInfo.insert(QStringLiteral("score_signal"), prefixRow.signal.scoreSignal);
                candidate.item.debugInfo.insert(QStringLiteral("avg_doc_boost"), prefixRow.signal.avgDocBoost);
                candidate.item.debugInfo.insert(
                    QStringLiteral("merged_field_mask"), static_cast<qint64>(prefixRow.signal.mergedFieldMask));
            }

            acceptSuggestion(std::move(candidate));
        }
    }

    const bool shouldRunTermSupplement = !skipExpensiveIndexScan || !options.enablePrefix;
    if (shouldRunTermSupplement) {
        int termSupplementCount = 0;
        repository_->forEachTermEntry([&](const QString& key, const QVector<domain::models::PostingEntry>& postings) {
            if (termSupplementCount >= maxResults * 8) {
                return;
            }
            if (!matchesAnyRelatedTerm(key, queryKeys)) {
                return;
            }
            if (isLowQualitySuggestionText(key)) {
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
            candidate.item.suggestKind = QStringLiteral("term");
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
    }

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
