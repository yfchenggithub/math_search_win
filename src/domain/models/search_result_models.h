#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace domain::models {

struct SearchOptions {
    int maxResults = 20;
    bool enablePrefix = true;
    bool enableTerm = true;
    bool enableExactBoost = true;
    bool enableDebug = false;
    QStringList moduleFilter;
    QStringList categoryFilter;
    QStringList tagFilter;
};

struct SearchHit {
    QString docId;
    QString title;
    QString module;
    QString category;
    double difficulty = 0.0;
    QStringList tags;
    QString summary;
    QString coreFormula;
    double score = 0.0;
    QStringList matchedFields;
    QJsonObject debugInfo;
};

struct SearchResult {
    QString query;
    QString normalizedQuery;
    int total = 0;
    QVector<SearchHit> hits;
};

struct SuggestOptions {
    int maxResults = 8;
    bool enablePrefix = true;
    bool enableExactDedup = true;
    bool enableDebug = false;
    QStringList moduleFilter;
    QStringList categoryFilter;
    QStringList tagFilter;
};

struct SuggestionItem {
    QString text;
    QString normalizedText;
    double score = 0.0;
    QString source;
    QStringList matchedFields;
    QStringList targetDocIds;
    QJsonObject debugInfo;
};

struct SuggestionResult {
    QString query;
    QString normalizedQuery;
    int total = 0;
    QVector<SuggestionItem> items;
};

QString normalizeQueryText(const QString& rawQuery);

}  // namespace domain::models
