#include "infrastructure/data/backend_search_index_loader.h"

#include "shared/paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <limits>

namespace infrastructure::data {
namespace {

using domain::models::BackendSearchIndex;
using domain::models::FieldMaskLegend;
using domain::models::IndexDocRecord;
using domain::models::IndexedSuggestionSeed;
using domain::models::ModuleBuildStat;
using domain::models::PostingEntry;
using domain::models::SearchBuildOptions;
using domain::models::SearchIndexStats;

constexpr auto kDefaultFileName = "backend_search_index.json";

void addWarning(BackendSearchIndexDiagnostics& diagnostics, const QString& scope, const QString& message)
{
    if (scope.isEmpty()) {
        diagnostics.warnings.push_back(message);
        return;
    }
    diagnostics.warnings.push_back(QStringLiteral("%1: %2").arg(scope, message));
}

bool failWithFatal(BackendSearchIndexDiagnostics& diagnostics, const QString& message)
{
    diagnostics.fatalError = message;
    diagnostics.success = false;
    return false;
}

QString valueToString(const QJsonValue& value, bool* converted)
{
    if (converted != nullptr) {
        *converted = false;
    }
    if (value.isString()) {
        return value.toString();
    }
    if (value.isDouble()) {
        if (converted != nullptr) {
            *converted = true;
        }
        return QString::number(value.toDouble(), 'g', 16);
    }
    if (value.isBool()) {
        if (converted != nullptr) {
            *converted = true;
        }
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    return {};
}

QString readString(const QJsonObject& object,
                   const QString& key,
                   const QString& scope,
                   BackendSearchIndexDiagnostics& diagnostics)
{
    if (!object.contains(key)) {
        return {};
    }
    const QJsonValue value = object.value(key);
    if (value.isNull() || value.isUndefined()) {
        return {};
    }

    bool converted = false;
    const QString output = valueToString(value, &converted);
    if (!output.isNull()) {
        if (converted) {
            addWarning(diagnostics, scope, QStringLiteral("field '%1' converted to string").arg(key));
        }
        return output;
    }

    addWarning(diagnostics, scope, QStringLiteral("field '%1' expected string, fallback empty").arg(key));
    return {};
}

int readInt(const QJsonObject& object,
            const QString& key,
            const QString& scope,
            BackendSearchIndexDiagnostics& diagnostics,
            int fallback = 0)
{
    if (!object.contains(key)) {
        return fallback;
    }
    const QJsonValue value = object.value(key);
    if (value.isDouble()) {
        return static_cast<int>(value.toDouble());
    }
    if (value.isString()) {
        bool ok = false;
        const int converted = value.toString().trimmed().toInt(&ok);
        if (ok) {
            addWarning(diagnostics, scope, QStringLiteral("field '%1' converted from string to int").arg(key));
            return converted;
        }
    }
    addWarning(diagnostics, scope, QStringLiteral("field '%1' expected int, fallback used").arg(key));
    return fallback;
}

double readDouble(const QJsonObject& object,
                  const QString& key,
                  const QString& scope,
                  BackendSearchIndexDiagnostics& diagnostics,
                  double fallback = 0.0)
{
    if (!object.contains(key)) {
        return fallback;
    }
    const QJsonValue value = object.value(key);
    if (value.isDouble()) {
        return value.toDouble();
    }
    if (value.isString()) {
        bool ok = false;
        const double converted = value.toString().trimmed().toDouble(&ok);
        if (ok) {
            addWarning(diagnostics, scope, QStringLiteral("field '%1' converted from string to number").arg(key));
            return converted;
        }
    }
    addWarning(diagnostics, scope, QStringLiteral("field '%1' expected number, fallback used").arg(key));
    return fallback;
}

QStringList readStringList(const QJsonObject& object,
                           const QString& key,
                           const QString& scope,
                           BackendSearchIndexDiagnostics& diagnostics)
{
    QStringList values;
    if (!object.contains(key)) {
        return values;
    }

    const QJsonValue rawValue = object.value(key);
    if (rawValue.isNull() || rawValue.isUndefined()) {
        return values;
    }
    if (!rawValue.isArray()) {
        addWarning(diagnostics, scope, QStringLiteral("field '%1' expected array, fallback empty").arg(key));
        return values;
    }

    const QJsonArray array = rawValue.toArray();
    values.reserve(array.size());
    for (qsizetype i = 0; i < array.size(); ++i) {
        bool converted = false;
        const QString text = valueToString(array.at(i), &converted);
        if (text.isNull()) {
            addWarning(diagnostics,
                       scope,
                       QStringLiteral("field '%1[%2]' expected string item, skipped").arg(key).arg(i));
            continue;
        }
        if (converted) {
            addWarning(diagnostics,
                       scope,
                       QStringLiteral("field '%1[%2]' converted to string").arg(key).arg(i));
        }
        values.push_back(text);
    }
    return values;
}

bool readRequiredObject(const QJsonObject& root,
                        const QString& fieldName,
                        QJsonObject* out,
                        BackendSearchIndexDiagnostics& diagnostics)
{
    if (out == nullptr) {
        return false;
    }

    if (!root.contains(fieldName)) {
        return failWithFatal(diagnostics, QStringLiteral("missing top-level field '%1'").arg(fieldName));
    }
    const QJsonValue rawValue = root.value(fieldName);
    if (!rawValue.isObject()) {
        return failWithFatal(diagnostics, QStringLiteral("top-level field '%1' must be object/map").arg(fieldName));
    }
    *out = rawValue.toObject();
    return true;
}

ModuleBuildStat parseModuleStat(const QJsonObject& object,
                                const QString& scope,
                                BackendSearchIndexDiagnostics& diagnostics)
{
    ModuleBuildStat stat;
    stat.module = readString(object, QStringLiteral("module"), scope, diagnostics).trimmed();
    stat.scanned = readInt(object, QStringLiteral("scanned"), scope, diagnostics);
    stat.built = readInt(object, QStringLiteral("built"), scope, diagnostics);
    stat.filtered = readInt(object, QStringLiteral("filtered"), scope, diagnostics);
    stat.skipped = readInt(object, QStringLiteral("skipped"), scope, diagnostics);
    return stat;
}

SearchIndexStats parseStats(const QJsonObject& statsObject, BackendSearchIndexDiagnostics& diagnostics)
{
    SearchIndexStats stats;
    const QString scope = QStringLiteral("stats");
    stats.documents = readInt(statsObject, QStringLiteral("documents"), scope, diagnostics);
    stats.terms = readInt(statsObject, QStringLiteral("terms"), scope, diagnostics);
    stats.prefixes = readInt(statsObject, QStringLiteral("prefixes"), scope, diagnostics);
    stats.suggestions = readInt(statsObject, QStringLiteral("suggestions"), scope, diagnostics);
    stats.modules = readInt(statsObject, QStringLiteral("modules"), scope, diagnostics);

    const QJsonValue moduleStatsValue = statsObject.value(QStringLiteral("moduleStats"));
    if (!moduleStatsValue.isNull() && !moduleStatsValue.isUndefined()) {
        if (!moduleStatsValue.isArray()) {
            addWarning(diagnostics, scope, QStringLiteral("field 'moduleStats' expected array, ignored"));
        } else {
            const QJsonArray moduleStatsArray = moduleStatsValue.toArray();
            stats.moduleStats.reserve(moduleStatsArray.size());
            for (qsizetype i = 0; i < moduleStatsArray.size(); ++i) {
                if (!moduleStatsArray.at(i).isObject()) {
                    addWarning(diagnostics,
                               scope,
                               QStringLiteral("field 'moduleStats[%1]' expected object, skipped").arg(i));
                    continue;
                }
                stats.moduleStats.push_back(
                    parseModuleStat(moduleStatsArray.at(i).toObject(),
                                    QStringLiteral("%1.moduleStats[%2]").arg(scope).arg(i),
                                    diagnostics));
            }
        }
    }

    return stats;
}

SearchBuildOptions parseBuildOptions(const QJsonObject& object, BackendSearchIndexDiagnostics& diagnostics)
{
    SearchBuildOptions options;
    const QString scope = QStringLiteral("buildOptions");
    options.prefixDocLimit = readInt(object, QStringLiteral("prefixDocLimit"), scope, diagnostics);
    options.suggestionLimit = readInt(object, QStringLiteral("suggestionLimit"), scope, diagnostics);
    options.targetModules = readStringList(object, QStringLiteral("targetModules"), scope, diagnostics);
    options.targetItems = readStringList(object, QStringLiteral("targetItems"), scope, diagnostics);
    return options;
}

bool parseFieldMaskLegend(const QJsonObject& object,
                          FieldMaskLegend* outLegend,
                          BackendSearchIndexDiagnostics& diagnostics)
{
    if (outLegend == nullptr) {
        return false;
    }

    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!it.value().isDouble()) {
            addWarning(diagnostics,
                       QStringLiteral("fieldMaskLegend"),
                       QStringLiteral("entry '%1' expected integer bit, skipped").arg(it.key()));
            continue;
        }

        const double rawValue = it.value().toDouble();
        if (rawValue <= 0.0
            || rawValue > static_cast<double>(std::numeric_limits<quint32>::max())
            || rawValue != static_cast<double>(static_cast<quint32>(rawValue))) {
            addWarning(diagnostics,
                       QStringLiteral("fieldMaskLegend"),
                       QStringLiteral("entry '%1' has invalid bit value, skipped").arg(it.key()));
            continue;
        }

        outLegend->insert(it.key(), static_cast<quint32>(rawValue));
    }
    return true;
}

bool parseDoc(const QString& topLevelKey,
              const QJsonValue& rawDoc,
              IndexDocRecord* outDoc,
              QString* outRepoKey,
              BackendSearchIndexDiagnostics& diagnostics)
{
    if (outDoc == nullptr || outRepoKey == nullptr) {
        return false;
    }

    const QString key = topLevelKey.trimmed();
    if (key.isEmpty()) {
        addWarning(diagnostics, QStringLiteral("docs"), QStringLiteral("empty doc key encountered and skipped"));
        return false;
    }
    if (!rawDoc.isObject()) {
        addWarning(diagnostics, QStringLiteral("docs.%1").arg(key), QStringLiteral("doc value is not object, skipped"));
        return false;
    }

    const QJsonObject object = rawDoc.toObject();
    IndexDocRecord doc;
    doc.id = readString(object, QStringLiteral("id"), QStringLiteral("docs.%1").arg(key), diagnostics).trimmed();
    if (doc.id.isEmpty()) {
        // Repository key is always the top-level map key. This keeps identity stable even when source id is empty.
        doc.id = key;
        addWarning(diagnostics,
                   QStringLiteral("docs.%1").arg(key),
                   QStringLiteral("doc.id missing, fallback to top-level key '%1'").arg(key));
    } else if (doc.id != key) {
        addWarning(diagnostics,
                   QStringLiteral("docs.%1").arg(key),
                   QStringLiteral("doc.id '%1' mismatches top-level key '%2'; key takes precedence").arg(doc.id, key));
        doc.id = key;
    }

    doc.module = readString(object, QStringLiteral("module"), QStringLiteral("docs.%1").arg(key), diagnostics);
    doc.moduleDir = readString(object, QStringLiteral("moduleDir"), QStringLiteral("docs.%1").arg(key), diagnostics);
    doc.title = readString(object, QStringLiteral("title"), QStringLiteral("docs.%1").arg(key), diagnostics);
    doc.summary = readString(object, QStringLiteral("summary"), QStringLiteral("docs.%1").arg(key), diagnostics);
    doc.category = readString(object, QStringLiteral("category"), QStringLiteral("docs.%1").arg(key), diagnostics);
    doc.tags = readStringList(object, QStringLiteral("tags"), QStringLiteral("docs.%1").arg(key), diagnostics);
    doc.coreFormula = readString(object, QStringLiteral("coreFormula"), QStringLiteral("docs.%1").arg(key), diagnostics);
    doc.rank = readInt(object, QStringLiteral("rank"), QStringLiteral("docs.%1").arg(key), diagnostics);
    doc.difficulty = readDouble(object, QStringLiteral("difficulty"), QStringLiteral("docs.%1").arg(key), diagnostics);
    doc.searchBoost = readDouble(object, QStringLiteral("searchBoost"), QStringLiteral("docs.%1").arg(key), diagnostics);
    doc.hotScore = readDouble(object, QStringLiteral("hotScore"), QStringLiteral("docs.%1").arg(key), diagnostics);
    doc.examFrequency = readDouble(object, QStringLiteral("examFrequency"), QStringLiteral("docs.%1").arg(key), diagnostics);
    doc.examScore = readDouble(object, QStringLiteral("examScore"), QStringLiteral("docs.%1").arg(key), diagnostics);

    *outDoc = std::move(doc);
    *outRepoKey = key;
    return true;
}

using PostingTable = QHash<QString, QVector<PostingEntry>>;

void parsePostingTable(const QString& tableName,
                       const QJsonObject& tableObject,
                       const QHash<QString, IndexDocRecord>& docs,
                       PostingTable* outTable,
                       BackendSearchIndexDiagnostics& diagnostics,
                       qsizetype* outLoadedCount)
{
    if (outTable == nullptr || outLoadedCount == nullptr) {
        return;
    }

    for (auto it = tableObject.constBegin(); it != tableObject.constEnd(); ++it) {
        if (!it.value().isArray()) {
            addWarning(diagnostics,
                       tableName,
                       QStringLiteral("entry '%1' expected posting list array, skipped").arg(it.key()));
            continue;
        }

        const QJsonArray postingArray = it.value().toArray();
        QVector<PostingEntry> postings;
        postings.reserve(postingArray.size());

        for (qsizetype i = 0; i < postingArray.size(); ++i) {
            const QJsonValue postingRaw = postingArray.at(i);
            if (!postingRaw.isArray()) {
                addWarning(diagnostics,
                           tableName,
                           QStringLiteral("entry '%1[%2]' expected posting tuple array").arg(it.key()).arg(i));
                ++diagnostics.skippedPostingCount;
                continue;
            }

            const QJsonArray tuple = postingRaw.toArray();
            if (tuple.size() != 3) {
                addWarning(diagnostics,
                           tableName,
                           QStringLiteral("entry '%1[%2]' tuple length must be 3").arg(it.key()).arg(i));
                ++diagnostics.skippedPostingCount;
                continue;
            }

            if (!tuple.at(0).isString()) {
                addWarning(diagnostics,
                           tableName,
                           QStringLiteral("entry '%1[%2]' docId must be string").arg(it.key()).arg(i));
                ++diagnostics.skippedPostingCount;
                continue;
            }
            const QString docId = tuple.at(0).toString().trimmed();
            if (docId.isEmpty() || !docs.contains(docId)) {
                addWarning(diagnostics,
                           tableName,
                           QStringLiteral("entry '%1[%2]' docId '%3' not found in docs").arg(it.key()).arg(i).arg(docId));
                ++diagnostics.skippedPostingCount;
                continue;
            }

            if (!tuple.at(1).isDouble()) {
                addWarning(diagnostics,
                           tableName,
                           QStringLiteral("entry '%1[%2]' score must be number").arg(it.key()).arg(i));
                ++diagnostics.skippedPostingCount;
                continue;
            }
            const double score = tuple.at(1).toDouble();

            if (!tuple.at(2).isDouble()) {
                addWarning(diagnostics,
                           tableName,
                           QStringLiteral("entry '%1[%2]' fieldMask must be integer").arg(it.key()).arg(i));
                ++diagnostics.skippedPostingCount;
                continue;
            }
            const double rawMask = tuple.at(2).toDouble();
            if (rawMask < 0.0 || rawMask > static_cast<double>(std::numeric_limits<quint32>::max())
                || rawMask != static_cast<double>(static_cast<quint32>(rawMask))) {
                addWarning(diagnostics,
                           tableName,
                           QStringLiteral("entry '%1[%2]' fieldMask is invalid").arg(it.key()).arg(i));
                ++diagnostics.skippedPostingCount;
                continue;
            }

            PostingEntry entry;
            entry.docId = docId;
            entry.score = score;
            entry.fieldMask = static_cast<quint32>(rawMask);
            postings.push_back(std::move(entry));
        }

        if (!postings.isEmpty()) {
            outTable->insert(it.key(), std::move(postings));
        } else {
            addWarning(diagnostics,
                       tableName,
                       QStringLiteral("entry '%1' has no valid postings after validation").arg(it.key()));
        }
    }

    *outLoadedCount = outTable->size();
}

void parseOptionalSuggestions(const QJsonObject& root,
                              const QHash<QString, IndexDocRecord>& docs,
                              QVector<IndexedSuggestionSeed>* outSuggestions,
                              BackendSearchIndexDiagnostics& diagnostics)
{
    if (outSuggestions == nullptr || !root.contains(QStringLiteral("suggestions"))) {
        return;
    }

    const QJsonValue rawSuggestions = root.value(QStringLiteral("suggestions"));
    if (!rawSuggestions.isArray()) {
        addWarning(diagnostics, QStringLiteral("suggestions"), QStringLiteral("top-level suggestions is not array, ignored"));
        return;
    }

    const QJsonArray array = rawSuggestions.toArray();
    outSuggestions->reserve(array.size());
    for (qsizetype i = 0; i < array.size(); ++i) {
        if (!array.at(i).isArray()) {
            addWarning(diagnostics, QStringLiteral("suggestions"), QStringLiteral("item[%1] expected tuple array, skipped").arg(i));
            continue;
        }

        const QJsonArray tuple = array.at(i).toArray();
        if (tuple.size() < 3) {
            addWarning(diagnostics, QStringLiteral("suggestions"), QStringLiteral("item[%1] tuple size < 3, skipped").arg(i));
            continue;
        }

        if (!tuple.at(0).isString()) {
            addWarning(diagnostics, QStringLiteral("suggestions"), QStringLiteral("item[%1] text must be string").arg(i));
            continue;
        }
        if (!tuple.at(1).isString()) {
            addWarning(diagnostics, QStringLiteral("suggestions"), QStringLiteral("item[%1] docId must be string").arg(i));
            continue;
        }
        if (!tuple.at(2).isDouble()) {
            addWarning(diagnostics, QStringLiteral("suggestions"), QStringLiteral("item[%1] score must be number").arg(i));
            continue;
        }

        IndexedSuggestionSeed seed;
        seed.text = tuple.at(0).toString().trimmed();
        seed.docId = tuple.at(1).toString().trimmed();
        seed.score = tuple.at(2).toDouble();
        if (seed.text.isEmpty()) {
            addWarning(diagnostics, QStringLiteral("suggestions"), QStringLiteral("item[%1] text empty, skipped").arg(i));
            continue;
        }
        if (!seed.docId.isEmpty() && !docs.contains(seed.docId)) {
            addWarning(diagnostics,
                       QStringLiteral("suggestions"),
                       QStringLiteral("item[%1] references unknown docId '%2', skipped").arg(i).arg(seed.docId));
            continue;
        }

        outSuggestions->push_back(std::move(seed));
    }
}

}  // namespace

bool BackendSearchIndexDiagnostics::isSuccess() const
{
    return success && fatalError.isEmpty();
}

bool BackendSearchIndexLoadResult::isSuccess() const
{
    return diagnostics.isSuccess();
}

QString BackendSearchIndexLoader::defaultIndexPath()
{
    return QDir(AppPaths::dataDir()).filePath(QString::fromUtf8(kDefaultFileName));
}

BackendSearchIndexLoadResult BackendSearchIndexLoader::loadFromFile(const QString& filePath)
{
    BackendSearchIndexLoadResult result;

    const QString resolvedPath = filePath.trimmed().isEmpty() ? defaultIndexPath() : filePath.trimmed();
    const QFileInfo fileInfo(resolvedPath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        failWithFatal(result.diagnostics, QStringLiteral("index file not found: %1").arg(resolvedPath));
        return result;
    }

    QFile file(resolvedPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        failWithFatal(result.diagnostics,
                      QStringLiteral("failed to open index file: %1, reason=%2").arg(resolvedPath, file.errorString()));
        return result;
    }

    const QByteArray rawBytes = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument jsonDocument = QJsonDocument::fromJson(rawBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        failWithFatal(result.diagnostics,
                      QStringLiteral("failed to parse index JSON: %1 (offset=%2)")
                          .arg(parseError.errorString())
                          .arg(parseError.offset));
        return result;
    }
    if (!jsonDocument.isObject()) {
        failWithFatal(result.diagnostics, QStringLiteral("index JSON root must be object/map"));
        return result;
    }

    const QJsonObject root = jsonDocument.object();

    if (!root.contains(QStringLiteral("version"))) {
        failWithFatal(result.diagnostics, QStringLiteral("missing top-level field 'version'"));
        return result;
    }
    if (!root.contains(QStringLiteral("generatedAt"))) {
        failWithFatal(result.diagnostics, QStringLiteral("missing top-level field 'generatedAt'"));
        return result;
    }
    result.index.version = root.value(QStringLiteral("version")).toInt(0);
    if (!root.value(QStringLiteral("version")).isDouble()) {
        addWarning(result.diagnostics, QStringLiteral("root"), QStringLiteral("field 'version' converted to int"));
    }
    result.index.generatedAt = valueToString(root.value(QStringLiteral("generatedAt")), nullptr);

    QJsonObject statsObject;
    QJsonObject buildOptionsObject;
    QJsonObject fieldMaskLegendObject;
    QJsonObject docsObject;
    QJsonObject termIndexObject;
    QJsonObject prefixIndexObject;
    if (!readRequiredObject(root, QStringLiteral("stats"), &statsObject, result.diagnostics)
        || !readRequiredObject(root, QStringLiteral("buildOptions"), &buildOptionsObject, result.diagnostics)
        || !readRequiredObject(root, QStringLiteral("fieldMaskLegend"), &fieldMaskLegendObject, result.diagnostics)
        || !readRequiredObject(root, QStringLiteral("docs"), &docsObject, result.diagnostics)
        || !readRequiredObject(root, QStringLiteral("termIndex"), &termIndexObject, result.diagnostics)
        || !readRequiredObject(root, QStringLiteral("prefixIndex"), &prefixIndexObject, result.diagnostics)) {
        return result;
    }

    result.index.stats = parseStats(statsObject, result.diagnostics);
    result.index.buildOptions = parseBuildOptions(buildOptionsObject, result.diagnostics);
    parseFieldMaskLegend(fieldMaskLegendObject, &result.index.fieldMaskLegend, result.diagnostics);

    for (auto it = docsObject.constBegin(); it != docsObject.constEnd(); ++it) {
        IndexDocRecord doc;
        QString repoKey;
        if (!parseDoc(it.key(), it.value(), &doc, &repoKey, result.diagnostics)) {
            ++result.diagnostics.skippedDocCount;
            continue;
        }
        result.index.docs.insert(repoKey, std::move(doc));
    }
    result.diagnostics.loadedDocCount = result.index.docs.size();

    parsePostingTable(QStringLiteral("termIndex"),
                      termIndexObject,
                      result.index.docs,
                      &result.index.termIndex,
                      result.diagnostics,
                      &result.diagnostics.loadedTermCount);
    parsePostingTable(QStringLiteral("prefixIndex"),
                      prefixIndexObject,
                      result.index.docs,
                      &result.index.prefixIndex,
                      result.diagnostics,
                      &result.diagnostics.loadedPrefixCount);

    parseOptionalSuggestions(root, result.index.docs, &result.index.suggestions, result.diagnostics);

    result.diagnostics.success = result.diagnostics.fatalError.isEmpty();
    return result;
}

}  // namespace infrastructure::data
