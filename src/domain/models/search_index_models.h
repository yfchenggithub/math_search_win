#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace domain::models {

struct ModuleBuildStat {
    QString module;
    int scanned = 0;
    int built = 0;
    int filtered = 0;
    int skipped = 0;
};

struct SearchIndexStats {
    int documents = 0;
    int terms = 0;
    int prefixes = 0;
    int suggestions = 0;
    int modules = 0;
    QVector<ModuleBuildStat> moduleStats;
};

struct SearchBuildOptions {
    int prefixDocLimit = 0;
    int suggestionLimit = 0;
    QStringList targetModules;
    QStringList targetItems;
};

struct IndexDocRecord {
    QString id;
    QString module;
    QString moduleDir;
    QString title;
    QString summary;
    QString category;
    QStringList tags;
    QString coreFormula;
    int rank = 0;
    double difficulty = 0.0;
    double searchBoost = 0.0;
    double hotScore = 0.0;
    double examFrequency = 0.0;
    double examScore = 0.0;
};

struct PostingEntry {
    QString docId;
    double score = 0.0;
    quint32 fieldMask = 0;
};

struct IndexedSuggestionSeed {
    QString text;
    QString docId;
    double score = 0.0;
};

using FieldMaskLegend = QHash<QString, quint32>;

struct BackendSearchIndex {
    int version = 0;
    QString generatedAt;
    SearchIndexStats stats;
    SearchBuildOptions buildOptions;
    FieldMaskLegend fieldMaskLegend;
    QHash<QString, IndexDocRecord> docs;
    QHash<QString, QVector<PostingEntry>> termIndex;
    QHash<QString, QVector<PostingEntry>> prefixIndex;

    // Optional top-level suggestions payload from index build output.
    QVector<IndexedSuggestionSeed> suggestions;
};

QStringList decodeFieldMask(quint32 fieldMask, const FieldMaskLegend& legend);

}  // namespace domain::models

