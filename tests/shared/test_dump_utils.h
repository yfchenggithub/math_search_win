#pragma once

#include "domain/models/search_result_models.h"

#include <QString>
#include <QStringList>

#include <algorithm>

namespace tests::shared {

inline QString boolToString(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

inline QString summarizeList(const QStringList& values)
{
    if (values.isEmpty()) {
        return QStringLiteral("<none>");
    }
    return values.join(QStringLiteral("|"));
}

inline QString describeSearchOptions(const domain::models::SearchOptions& options)
{
    return QStringLiteral(
               "maxResults=%1 enablePrefix=%2 enableTerm=%3 enableExactBoost=%4 enableDebug=%5 moduleFilter=[%6] "
               "categoryFilter=[%7] tagFilter=[%8]")
        .arg(options.maxResults)
        .arg(boolToString(options.enablePrefix))
        .arg(boolToString(options.enableTerm))
        .arg(boolToString(options.enableExactBoost))
        .arg(boolToString(options.enableDebug))
        .arg(summarizeList(options.moduleFilter))
        .arg(summarizeList(options.categoryFilter))
        .arg(summarizeList(options.tagFilter));
}

inline QString describeSuggestOptions(const domain::models::SuggestOptions& options)
{
    return QStringLiteral(
               "maxResults=%1 enablePrefix=%2 enableExactDedup=%3 enableDebug=%4 moduleFilter=[%5] categoryFilter=[%6] "
               "tagFilter=[%7]")
        .arg(options.maxResults)
        .arg(boolToString(options.enablePrefix))
        .arg(boolToString(options.enableExactDedup))
        .arg(boolToString(options.enableDebug))
        .arg(summarizeList(options.moduleFilter))
        .arg(summarizeList(options.categoryFilter))
        .arg(summarizeList(options.tagFilter));
}

inline QString summarizeSearchHits(const domain::models::SearchResult& result, int maxItems = 5)
{
    QStringList rows;
    rows.push_back(QStringLiteral("query=%1 normalized=%2 total=%3 returned=%4")
                       .arg(result.query)
                       .arg(result.normalizedQuery)
                       .arg(result.total)
                       .arg(result.hits.size()));
    const int limit = std::min(maxItems, static_cast<int>(result.hits.size()));
    for (int i = 0; i < limit; ++i) {
        const auto& hit = result.hits.at(i);
        rows.push_back(QStringLiteral("#%1 docId=%2 title=%3 module=%4 category=%5 score=%6")
                           .arg(i)
                           .arg(hit.docId)
                           .arg(hit.title)
                           .arg(hit.module)
                           .arg(hit.category)
                           .arg(hit.score, 0, 'f', 3));
    }
    return rows.join(QStringLiteral(" || "));
}

inline QString summarizeSuggestions(const domain::models::SuggestionResult& result, int maxItems = 5)
{
    QStringList rows;
    rows.push_back(QStringLiteral("query=%1 normalized=%2 total=%3 returned=%4")
                       .arg(result.query)
                       .arg(result.normalizedQuery)
                       .arg(result.total)
                       .arg(result.items.size()));
    const int limit = std::min(maxItems, static_cast<int>(result.items.size()));
    for (int i = 0; i < limit; ++i) {
        const auto& item = result.items.at(i);
        rows.push_back(QStringLiteral("#%1 text=%2 source=%3 score=%4 targets=%5")
                           .arg(i)
                           .arg(item.text)
                           .arg(item.source)
                           .arg(item.score, 0, 'f', 3)
                           .arg(item.targetDocIds.join(QStringLiteral(","))));
    }
    return rows.join(QStringLiteral(" || "));
}

}  // namespace tests::shared
