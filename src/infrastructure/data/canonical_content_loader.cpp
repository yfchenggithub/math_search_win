#include "infrastructure/data/canonical_content_loader.h"

#include "domain/models/render_section.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <algorithm>
#include <initializer_list>

namespace infrastructure::data {
namespace {

using domain::models::AssetRefs;
using domain::models::ConclusionContent;
using domain::models::ConclusionDef;
using domain::models::ConclusionExt;
using domain::models::ConclusionIdentity;
using domain::models::ConclusionMeta;
using domain::models::ConclusionRecord;
using domain::models::ConditionDef;
using domain::models::ContentFragment;
using domain::models::ExamInfo;
using domain::models::PlainContent;
using domain::models::RelationsInfo;
using domain::models::RenderBlock;
using domain::models::RenderSection;
using domain::models::RenderToken;
using domain::models::ShareInfo;
using domain::models::TheoremGroupItem;
using domain::models::VariableDef;

constexpr auto kDefaultRelativePath = "data/canonical_content_v2.json";

QString buildScopeKey(const QString& recordId, const QString& scope)
{
    if (recordId.isEmpty()) {
        return scope;
    }
    return QStringLiteral("record[%1].%2").arg(recordId, scope);
}

void addWarning(CanonicalContentDiagnostics& diagnostics, const QString& scopedKey, const QString& message)
{
    if (scopedKey.isEmpty()) {
        diagnostics.warnings.push_back(message);
        return;
    }
    diagnostics.warnings.push_back(QStringLiteral("%1: %2").arg(scopedKey, message));
}

QString toStringLossy(const QJsonValue& value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'g', 16);
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    return {};
}

QString readString(const QJsonObject& object,
                   const QString& key,
                   const QString& scopedKey,
                   CanonicalContentDiagnostics& diagnostics)
{
    if (!object.contains(key)) {
        return {};
    }
    const QJsonValue value = object.value(key);
    if (value.isNull() || value.isUndefined()) {
        return {};
    }

    const QString converted = toStringLossy(value);
    if (!converted.isNull()) {
        if (!value.isString()) {
            addWarning(diagnostics,
                       scopedKey,
                       QStringLiteral("field '%1' expected string, converted from primitive").arg(key));
        }
        return converted;
    }

    addWarning(diagnostics, scopedKey, QStringLiteral("field '%1' expected string, fallback to empty").arg(key));
    return {};
}

int readInt(const QJsonObject& object,
            const QString& key,
            const QString& scopedKey,
            CanonicalContentDiagnostics& diagnostics,
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
        const int parsed = value.toString().trimmed().toInt(&ok);
        if (ok) {
            addWarning(diagnostics, scopedKey, QStringLiteral("field '%1' converted from string to int").arg(key));
            return parsed;
        }
    }
    if (!value.isNull() && !value.isUndefined()) {
        addWarning(diagnostics, scopedKey, QStringLiteral("field '%1' expected int, fallback used").arg(key));
    }
    return fallback;
}

double readDouble(const QJsonObject& object,
                  const QString& key,
                  const QString& scopedKey,
                  CanonicalContentDiagnostics& diagnostics,
                  bool* hasValue = nullptr)
{
    if (hasValue != nullptr) {
        *hasValue = false;
    }
    if (!object.contains(key)) {
        return 0.0;
    }

    const QJsonValue value = object.value(key);
    if (value.isDouble()) {
        if (hasValue != nullptr) {
            *hasValue = true;
        }
        return value.toDouble();
    }
    if (value.isString()) {
        bool ok = false;
        const double parsed = value.toString().trimmed().toDouble(&ok);
        if (ok) {
            if (hasValue != nullptr) {
                *hasValue = true;
            }
            addWarning(diagnostics, scopedKey, QStringLiteral("field '%1' converted from string to number").arg(key));
            return parsed;
        }
    }
    if (!value.isNull() && !value.isUndefined()) {
        addWarning(diagnostics, scopedKey, QStringLiteral("field '%1' expected number, fallback used").arg(key));
    }
    return 0.0;
}

bool readBool(const QJsonObject& object,
              const QString& key,
              const QString& scopedKey,
              CanonicalContentDiagnostics& diagnostics,
              bool fallback = false)
{
    if (!object.contains(key)) {
        return fallback;
    }

    const QJsonValue value = object.value(key);
    if (value.isBool()) {
        return value.toBool();
    }
    if (value.isString()) {
        const QString normalized = value.toString().trimmed().toLower();
        if (normalized == QStringLiteral("true") || normalized == QStringLiteral("1") || normalized == QStringLiteral("yes")
            || normalized == QStringLiteral("on")) {
            addWarning(diagnostics, scopedKey, QStringLiteral("field '%1' converted from string to bool").arg(key));
            return true;
        }
        if (normalized == QStringLiteral("false") || normalized == QStringLiteral("0")
            || normalized == QStringLiteral("no") || normalized == QStringLiteral("off")) {
            addWarning(diagnostics, scopedKey, QStringLiteral("field '%1' converted from string to bool").arg(key));
            return false;
        }
    }
    if (!value.isNull() && !value.isUndefined()) {
        addWarning(diagnostics, scopedKey, QStringLiteral("field '%1' expected bool, fallback used").arg(key));
    }
    return fallback;
}

QStringList readStringList(const QJsonObject& object,
                           const QString& key,
                           const QString& scopedKey,
                           CanonicalContentDiagnostics& diagnostics)
{
    QStringList result;
    if (!object.contains(key)) {
        return result;
    }

    const QJsonValue value = object.value(key);
    if (value.isNull() || value.isUndefined()) {
        return result;
    }
    if (!value.isArray()) {
        addWarning(diagnostics, scopedKey, QStringLiteral("field '%1' expected array, fallback to empty").arg(key));
        return result;
    }

    const QJsonArray array = value.toArray();
    result.reserve(array.size());
    for (qsizetype index = 0; index < array.size(); ++index) {
        const QJsonValue item = array.at(index);
        const QString itemText = toStringLossy(item);
        if (!itemText.isNull()) {
            result.push_back(itemText);
            if (!item.isString()) {
                addWarning(diagnostics,
                           scopedKey,
                           QStringLiteral("field '%1[%2]' converted from primitive to string").arg(key).arg(index));
            }
            continue;
        }
        addWarning(diagnostics,
                   scopedKey,
                   QStringLiteral("field '%1[%2]' expected string, item skipped").arg(key).arg(index));
    }
    return result;
}

QJsonObject parseExtraValue(const QJsonObject& object,
                            const QString& key,
                            const QString& scopedKey,
                            CanonicalContentDiagnostics& diagnostics)
{
    if (!object.contains(key)) {
        return {};
    }

    const QJsonValue value = object.value(key);
    if (value.isNull() || value.isUndefined()) {
        return {};
    }
    if (value.isObject()) {
        return value.toObject();
    }

    // Keep non-object "extra" payloads for compatibility, instead of hard-failing parsing.
    addWarning(diagnostics,
               scopedKey,
               QStringLiteral("field '%1' expected object, preserved under _raw_extra").arg(key));
    QJsonObject fallback;
    fallback.insert(QStringLiteral("_raw_extra"), value);
    return fallback;
}

QJsonObject collectUnknownFields(const QJsonObject& object, std::initializer_list<const char*> knownKeys)
{
    QJsonObject unknown = object;
    for (const char* key : knownKeys) {
        unknown.remove(QLatin1StringView(key));
    }
    return unknown;
}

void mergeUnknownIntoExtra(QJsonObject& extra, const QJsonObject& unknownFields)
{
    for (auto it = unknownFields.constBegin(); it != unknownFields.constEnd(); ++it) {
        extra.insert(it.key(), it.value());
    }
}

ContentFragment parseContentFragment(const QJsonObject& object,
                                     const QString& scopedKey,
                                     CanonicalContentDiagnostics& diagnostics)
{
    ContentFragment fragment;
    fragment.type = readString(object, QStringLiteral("type"), scopedKey, diagnostics);
    fragment.text = readString(object, QStringLiteral("text"), scopedKey, diagnostics);
    fragment.latex = readString(object, QStringLiteral("latex"), scopedKey, diagnostics);

    fragment.extra = parseExtraValue(object, QStringLiteral("extra"), scopedKey, diagnostics);
    mergeUnknownIntoExtra(fragment.extra, collectUnknownFields(object, {"type", "text", "latex", "extra"}));
    return fragment;
}

QVector<ContentFragment> parseContentFragments(const QJsonValue& value,
                                               const QString& scopedKey,
                                               CanonicalContentDiagnostics& diagnostics)
{
    QVector<ContentFragment> fragments;
    if (value.isNull() || value.isUndefined()) {
        return fragments;
    }
    if (!value.isArray()) {
        addWarning(diagnostics, scopedKey, QStringLiteral("expected array, fallback to empty"));
        return fragments;
    }

    const QJsonArray array = value.toArray();
    fragments.reserve(array.size());
    for (qsizetype i = 0; i < array.size(); ++i) {
        if (!array.at(i).isObject()) {
            addWarning(diagnostics, scopedKey, QStringLiteral("item[%1] is not object and was skipped").arg(i));
            continue;
        }
        fragments.push_back(parseContentFragment(array.at(i).toObject(),
                                                 QStringLiteral("%1.item[%2]").arg(scopedKey).arg(i),
                                                 diagnostics));
    }
    return fragments;
}

VariableDef parseVariable(const QJsonObject& object, const QString& scopedKey, CanonicalContentDiagnostics& diagnostics)
{
    VariableDef variable;
    variable.name = readString(object, QStringLiteral("name"), scopedKey, diagnostics);
    variable.latex = readString(object, QStringLiteral("latex"), scopedKey, diagnostics);
    variable.description = readString(object, QStringLiteral("description"), scopedKey, diagnostics);
    variable.required = readBool(object, QStringLiteral("required"), scopedKey, diagnostics);

    variable.extra = parseExtraValue(object, QStringLiteral("extra"), scopedKey, diagnostics);
    mergeUnknownIntoExtra(variable.extra, collectUnknownFields(object, {"name", "latex", "description", "required", "extra"}));
    return variable;
}

ConditionDef parseCondition(const QJsonObject& object, const QString& scopedKey, CanonicalContentDiagnostics& diagnostics)
{
    ConditionDef condition;
    condition.id = readString(object, QStringLiteral("id"), scopedKey, diagnostics);
    condition.title = readString(object, QStringLiteral("title"), scopedKey, diagnostics);
    condition.required = readBool(object, QStringLiteral("required"), scopedKey, diagnostics);
    condition.scope = readString(object, QStringLiteral("scope"), scopedKey, diagnostics);
    condition.content =
        parseContentFragments(object.value(QStringLiteral("content")), QStringLiteral("%1.content").arg(scopedKey), diagnostics);

    condition.extra = parseExtraValue(object, QStringLiteral("extra"), scopedKey, diagnostics);
    mergeUnknownIntoExtra(condition.extra, collectUnknownFields(object, {"id", "title", "content", "required", "scope", "extra"}));
    return condition;
}

ConclusionDef parseConclusion(const QJsonObject& object,
                              const QString& scopedKey,
                              CanonicalContentDiagnostics& diagnostics)
{
    ConclusionDef conclusion;
    conclusion.id = readString(object, QStringLiteral("id"), scopedKey, diagnostics);
    conclusion.title = readString(object, QStringLiteral("title"), scopedKey, diagnostics);
    conclusion.content =
        parseContentFragments(object.value(QStringLiteral("content")), QStringLiteral("%1.content").arg(scopedKey), diagnostics);

    conclusion.extra = parseExtraValue(object, QStringLiteral("extra"), scopedKey, diagnostics);
    mergeUnknownIntoExtra(conclusion.extra, collectUnknownFields(object, {"id", "title", "content", "extra"}));
    return conclusion;
}

RenderToken parseRenderToken(const QJsonObject& object,
                             const QString& scopedKey,
                             CanonicalContentDiagnostics& diagnostics)
{
    RenderToken token;
    token.type = readString(object, QStringLiteral("type"), scopedKey, diagnostics);
    token.knownType = domain::models::renderTokenKnownTypeFromString(token.type);
    token.text = readString(object, QStringLiteral("text"), scopedKey, diagnostics);
    token.latex = readString(object, QStringLiteral("latex"), scopedKey, diagnostics);

    token.extra = parseExtraValue(object, QStringLiteral("extra"), scopedKey, diagnostics);
    mergeUnknownIntoExtra(token.extra, collectUnknownFields(object, {"type", "text", "latex", "extra"}));
    return token;
}

QVector<RenderToken> parseRenderTokens(const QJsonValue& value,
                                       const QString& scopedKey,
                                       CanonicalContentDiagnostics& diagnostics)
{
    QVector<RenderToken> tokens;
    if (value.isNull() || value.isUndefined()) {
        return tokens;
    }
    if (!value.isArray()) {
        addWarning(diagnostics, scopedKey, QStringLiteral("expected array, fallback to empty"));
        return tokens;
    }

    const QJsonArray array = value.toArray();
    tokens.reserve(array.size());
    for (qsizetype i = 0; i < array.size(); ++i) {
        if (!array.at(i).isObject()) {
            addWarning(diagnostics, scopedKey, QStringLiteral("item[%1] is not object and was skipped").arg(i));
            continue;
        }
        tokens.push_back(parseRenderToken(array.at(i).toObject(),
                                          QStringLiteral("%1.item[%2]").arg(scopedKey).arg(i),
                                          diagnostics));
    }
    return tokens;
}

TheoremGroupItem parseTheoremGroupItem(const QJsonObject& object,
                                       const QString& scopedKey,
                                       CanonicalContentDiagnostics& diagnostics)
{
    TheoremGroupItem item;
    item.title = readString(object, QStringLiteral("title"), scopedKey, diagnostics);
    item.formulaLatex = readString(object, QStringLiteral("formula_latex"), scopedKey, diagnostics);
    item.descTokens =
        parseRenderTokens(object.value(QStringLiteral("desc_tokens")), QStringLiteral("%1.desc_tokens").arg(scopedKey), diagnostics);

    item.extra = parseExtraValue(object, QStringLiteral("extra"), scopedKey, diagnostics);
    mergeUnknownIntoExtra(item.extra, collectUnknownFields(object, {"title", "desc_tokens", "formula_latex", "extra"}));
    return item;
}

QVector<TheoremGroupItem> parseTheoremGroupItems(const QJsonValue& value,
                                                 const QString& scopedKey,
                                                 CanonicalContentDiagnostics& diagnostics)
{
    QVector<TheoremGroupItem> items;
    if (value.isNull() || value.isUndefined()) {
        return items;
    }
    if (!value.isArray()) {
        addWarning(diagnostics, scopedKey, QStringLiteral("expected array, fallback to empty"));
        return items;
    }

    const QJsonArray array = value.toArray();
    items.reserve(array.size());
    for (qsizetype i = 0; i < array.size(); ++i) {
        if (!array.at(i).isObject()) {
            addWarning(diagnostics, scopedKey, QStringLiteral("item[%1] is not object and was skipped").arg(i));
            continue;
        }
        items.push_back(parseTheoremGroupItem(array.at(i).toObject(),
                                              QStringLiteral("%1.item[%2]").arg(scopedKey).arg(i),
                                              diagnostics));
    }
    return items;
}

RenderBlock parseRenderBlock(const QJsonObject& object,
                             const QString& scopedKey,
                             CanonicalContentDiagnostics& diagnostics)
{
    RenderBlock block;
    block.id = readString(object, QStringLiteral("id"), scopedKey, diagnostics);
    block.type = readString(object, QStringLiteral("type"), scopedKey, diagnostics);
    block.knownType = domain::models::renderBlockKnownTypeFromString(block.type);
    block.latex = readString(object, QStringLiteral("latex"), scopedKey, diagnostics);
    block.align = readString(object, QStringLiteral("align"), scopedKey, diagnostics);
    block.tokens =
        parseRenderTokens(object.value(QStringLiteral("tokens")), QStringLiteral("%1.tokens").arg(scopedKey), diagnostics);
    block.items =
        parseTheoremGroupItems(object.value(QStringLiteral("items")), QStringLiteral("%1.items").arg(scopedKey), diagnostics);

    block.extra = parseExtraValue(object, QStringLiteral("extra"), scopedKey, diagnostics);
    mergeUnknownIntoExtra(
        block.extra, collectUnknownFields(object, {"id", "type", "latex", "align", "tokens", "items", "extra"}));
    return block;
}

QVector<RenderBlock> parseRenderBlocks(const QJsonValue& value,
                                       const QString& scopedKey,
                                       CanonicalContentDiagnostics& diagnostics)
{
    QVector<RenderBlock> blocks;
    if (value.isNull() || value.isUndefined()) {
        return blocks;
    }
    if (!value.isArray()) {
        addWarning(diagnostics, scopedKey, QStringLiteral("expected array, fallback to empty"));
        return blocks;
    }

    const QJsonArray array = value.toArray();
    blocks.reserve(array.size());
    for (qsizetype i = 0; i < array.size(); ++i) {
        if (!array.at(i).isObject()) {
            addWarning(diagnostics, scopedKey, QStringLiteral("item[%1] is not object and was skipped").arg(i));
            continue;
        }
        blocks.push_back(parseRenderBlock(array.at(i).toObject(),
                                          QStringLiteral("%1.item[%2]").arg(scopedKey).arg(i),
                                          diagnostics));
    }
    return blocks;
}

RenderSection parseRenderSection(const QJsonObject& object,
                                 const QString& scopedKey,
                                 CanonicalContentDiagnostics& diagnostics)
{
    RenderSection section;
    section.key = readString(object, QStringLiteral("key"), scopedKey, diagnostics);
    section.title = readString(object, QStringLiteral("title"), scopedKey, diagnostics);
    section.blockType = readString(object, QStringLiteral("block_type"), scopedKey, diagnostics);
    section.blocks =
        parseRenderBlocks(object.value(QStringLiteral("blocks")), QStringLiteral("%1.blocks").arg(scopedKey), diagnostics);

    section.extra = parseExtraValue(object, QStringLiteral("extra"), scopedKey, diagnostics);
    mergeUnknownIntoExtra(section.extra, collectUnknownFields(object, {"key", "title", "block_type", "blocks", "extra"}));
    return section;
}

QVector<RenderSection> parseRenderSections(const QJsonValue& value,
                                           const QString& scopedKey,
                                           CanonicalContentDiagnostics& diagnostics)
{
    QVector<RenderSection> sections;
    if (value.isNull() || value.isUndefined()) {
        return sections;
    }
    if (!value.isArray()) {
        addWarning(diagnostics, scopedKey, QStringLiteral("expected array, fallback to empty"));
        return sections;
    }

    const QJsonArray array = value.toArray();
    sections.reserve(array.size());
    for (qsizetype i = 0; i < array.size(); ++i) {
        if (!array.at(i).isObject()) {
            addWarning(diagnostics, scopedKey, QStringLiteral("item[%1] is not object and was skipped").arg(i));
            continue;
        }
        sections.push_back(parseRenderSection(array.at(i).toObject(),
                                              QStringLiteral("%1.item[%2]").arg(scopedKey).arg(i),
                                              diagnostics));
    }
    return sections;
}

template <typename T, typename ParserFn>
QVector<T> parseObjectArray(const QJsonValue& value,
                            const QString& scopedKey,
                            CanonicalContentDiagnostics& diagnostics,
                            ParserFn parser)
{
    QVector<T> result;
    if (value.isNull() || value.isUndefined()) {
        return result;
    }
    if (!value.isArray()) {
        addWarning(diagnostics, scopedKey, QStringLiteral("expected array, fallback to empty"));
        return result;
    }

    const QJsonArray array = value.toArray();
    result.reserve(array.size());
    for (qsizetype i = 0; i < array.size(); ++i) {
        const QJsonValue item = array.at(i);
        if (!item.isObject()) {
            addWarning(diagnostics, scopedKey, QStringLiteral("item[%1] is not object and was skipped").arg(i));
            continue;
        }
        result.push_back(parser(item.toObject(), QStringLiteral("%1.item[%2]").arg(scopedKey).arg(i), diagnostics));
    }
    return result;
}

PlainContent parsePlainContent(const QJsonValue& value,
                               const QString& scopedKey,
                               CanonicalContentDiagnostics& diagnostics)
{
    PlainContent plain;
    if (value.isNull() || value.isUndefined()) {
        return plain;
    }
    if (!value.isObject()) {
        addWarning(diagnostics, scopedKey, QStringLiteral("expected object, fallback to empty"));
        return plain;
    }

    const QJsonObject object = value.toObject();
    plain.statement = readString(object, QStringLiteral("statement"), scopedKey, diagnostics);
    plain.explanation = readString(object, QStringLiteral("explanation"), scopedKey, diagnostics);
    plain.proof = readString(object, QStringLiteral("proof"), scopedKey, diagnostics);
    plain.examples = readString(object, QStringLiteral("examples"), scopedKey, diagnostics);
    plain.traps = readString(object, QStringLiteral("traps"), scopedKey, diagnostics);
    plain.summary = readString(object, QStringLiteral("summary"), scopedKey, diagnostics);

    plain.extra = parseExtraValue(object, QStringLiteral("extra"), scopedKey, diagnostics);
    mergeUnknownIntoExtra(
        plain.extra, collectUnknownFields(object, {"statement", "explanation", "proof", "examples", "traps", "summary", "extra"}));
    return plain;
}

ConclusionIdentity parseIdentity(const QJsonObject& object,
                                 const QString& scopedKey,
                                 CanonicalContentDiagnostics& diagnostics)
{
    ConclusionIdentity identity;
    identity.slug = readString(object, QStringLiteral("slug"), scopedKey, diagnostics);
    identity.module = readString(object, QStringLiteral("module"), scopedKey, diagnostics);
    identity.knowledgeNode = readString(object, QStringLiteral("knowledge_node"), scopedKey, diagnostics);
    identity.altNodes = readStringList(object, QStringLiteral("alt_nodes"), scopedKey, diagnostics);

    identity.extra = parseExtraValue(object, QStringLiteral("extra"), scopedKey, diagnostics);
    mergeUnknownIntoExtra(identity.extra, collectUnknownFields(object, {"slug", "module", "knowledge_node", "alt_nodes", "extra"}));
    return identity;
}

ConclusionMeta parseMeta(const QJsonObject& object, const QString& scopedKey, CanonicalContentDiagnostics& diagnostics)
{
    ConclusionMeta meta;
    meta.title = readString(object, QStringLiteral("title"), scopedKey, diagnostics);
    meta.aliases = readStringList(object, QStringLiteral("aliases"), scopedKey, diagnostics);
    meta.difficulty = readInt(object, QStringLiteral("difficulty"), scopedKey, diagnostics);
    meta.category = readString(object, QStringLiteral("category"), scopedKey, diagnostics);
    meta.tags = readStringList(object, QStringLiteral("tags"), scopedKey, diagnostics);
    meta.summary = readString(object, QStringLiteral("summary"), scopedKey, diagnostics);
    meta.isPro = readBool(object, QStringLiteral("is_pro"), scopedKey, diagnostics);
    meta.remarks = readString(object, QStringLiteral("remarks"), scopedKey, diagnostics);

    meta.extra = parseExtraValue(object, QStringLiteral("extra"), scopedKey, diagnostics);
    mergeUnknownIntoExtra(
        meta.extra,
        collectUnknownFields(object, {"title", "aliases", "difficulty", "category", "tags", "summary", "is_pro", "remarks", "extra"}));
    return meta;
}

ConclusionContent parseContent(const QJsonObject& object,
                               const QString& scopedKey,
                               CanonicalContentDiagnostics& diagnostics)
{
    ConclusionContent content;
    content.renderSchemaVersion = readInt(object, QStringLiteral("render_schema_version"), scopedKey, diagnostics);
    content.primaryFormula = readString(object, QStringLiteral("primary_formula"), scopedKey, diagnostics);

    content.variables = parseObjectArray<VariableDef>(
        object.value(QStringLiteral("variables")), QStringLiteral("%1.variables").arg(scopedKey), diagnostics, parseVariable);
    content.conditions = parseObjectArray<ConditionDef>(
        object.value(QStringLiteral("conditions")), QStringLiteral("%1.conditions").arg(scopedKey), diagnostics, parseCondition);
    content.conclusions = parseObjectArray<ConclusionDef>(
        object.value(QStringLiteral("conclusions")), QStringLiteral("%1.conclusions").arg(scopedKey), diagnostics, parseConclusion);
    content.sections = parseRenderSections(object.value(QStringLiteral("sections")),
                                           QStringLiteral("%1.sections").arg(scopedKey),
                                           diagnostics);

    // Plain text fields are fallback channels when structured sections are missing or partially available.
    content.plain = parsePlainContent(object.value(QStringLiteral("plain")), QStringLiteral("%1.plain").arg(scopedKey), diagnostics);

    content.extra = parseExtraValue(object, QStringLiteral("extra"), scopedKey, diagnostics);
    mergeUnknownIntoExtra(content.extra,
                          collectUnknownFields(object,
                                               {"render_schema_version",
                                                "primary_formula",
                                                "variables",
                                                "conditions",
                                                "conclusions",
                                                "sections",
                                                "plain",
                                                "extra"}));
    return content;
}

AssetRefs parseAssets(const QJsonValue& value, const QString& scopedKey, CanonicalContentDiagnostics& diagnostics)
{
    AssetRefs assets;
    if (value.isNull() || value.isUndefined()) {
        return assets;
    }
    if (!value.isObject()) {
        addWarning(diagnostics, scopedKey, QStringLiteral("expected object, fallback to empty"));
        return assets;
    }

    const QJsonObject object = value.toObject();
    assets.cover = readString(object, QStringLiteral("cover"), scopedKey, diagnostics);
    assets.svg = readString(object, QStringLiteral("svg"), scopedKey, diagnostics);
    assets.png = readString(object, QStringLiteral("png"), scopedKey, diagnostics);
    assets.pdf = readString(object, QStringLiteral("pdf"), scopedKey, diagnostics);
    assets.mp4 = readString(object, QStringLiteral("mp4"), scopedKey, diagnostics);

    assets.extra = parseExtraValue(object, QStringLiteral("extra"), scopedKey, diagnostics);
    mergeUnknownIntoExtra(assets.extra, collectUnknownFields(object, {"cover", "svg", "png", "pdf", "mp4", "extra"}));
    return assets;
}

ShareInfo parseShareInfo(const QJsonValue& value, const QString& scopedKey, CanonicalContentDiagnostics& diagnostics)
{
    ShareInfo share;
    if (value.isNull() || value.isUndefined()) {
        return share;
    }
    if (!value.isObject()) {
        addWarning(diagnostics, scopedKey, QStringLiteral("expected object, fallback to empty"));
        return share;
    }

    const QJsonObject object = value.toObject();
    share.title = readString(object, QStringLiteral("title"), scopedKey, diagnostics);
    share.desc = readString(object, QStringLiteral("desc"), scopedKey, diagnostics);
    share.extra = parseExtraValue(object, QStringLiteral("extra"), scopedKey, diagnostics);
    mergeUnknownIntoExtra(share.extra, collectUnknownFields(object, {"title", "desc", "extra"}));
    return share;
}

RelationsInfo parseRelationsInfo(const QJsonValue& value,
                                 const QString& scopedKey,
                                 CanonicalContentDiagnostics& diagnostics)
{
    RelationsInfo relations;
    if (value.isNull() || value.isUndefined()) {
        return relations;
    }
    if (!value.isObject()) {
        addWarning(diagnostics, scopedKey, QStringLiteral("expected object, fallback to empty"));
        return relations;
    }

    const QJsonObject object = value.toObject();
    relations.prerequisites = readStringList(object, QStringLiteral("prerequisites"), scopedKey, diagnostics);
    relations.relatedIds = readStringList(object, QStringLiteral("related_ids"), scopedKey, diagnostics);
    relations.similar = readString(object, QStringLiteral("similar"), scopedKey, diagnostics);
    relations.extra = parseExtraValue(object, QStringLiteral("extra"), scopedKey, diagnostics);
    mergeUnknownIntoExtra(
        relations.extra, collectUnknownFields(object, {"prerequisites", "related_ids", "similar", "extra"}));
    return relations;
}

ExamInfo parseExamInfo(const QJsonValue& value, const QString& scopedKey, CanonicalContentDiagnostics& diagnostics)
{
    ExamInfo exam;
    if (value.isNull() || value.isUndefined()) {
        return exam;
    }
    if (!value.isObject()) {
        addWarning(diagnostics, scopedKey, QStringLiteral("expected object, fallback to empty"));
        return exam;
    }

    const QJsonObject object = value.toObject();
    exam.frequency = readDouble(object, QStringLiteral("frequency"), scopedKey, diagnostics, &exam.hasFrequency);
    exam.score = readDouble(object, QStringLiteral("score"), scopedKey, diagnostics, &exam.hasScore);
    exam.extra = parseExtraValue(object, QStringLiteral("extra"), scopedKey, diagnostics);
    mergeUnknownIntoExtra(exam.extra, collectUnknownFields(object, {"frequency", "score", "extra"}));
    return exam;
}

ConclusionExt parseExt(const QJsonValue& value, const QString& scopedKey, CanonicalContentDiagnostics& diagnostics)
{
    ConclusionExt ext;
    if (value.isNull() || value.isUndefined()) {
        return ext;
    }
    if (!value.isObject()) {
        addWarning(diagnostics, scopedKey, QStringLiteral("expected object, fallback to empty"));
        return ext;
    }

    const QJsonObject object = value.toObject();
    ext.share = parseShareInfo(object.value(QStringLiteral("share")), QStringLiteral("%1.share").arg(scopedKey), diagnostics);
    ext.relations =
        parseRelationsInfo(object.value(QStringLiteral("relations")), QStringLiteral("%1.relations").arg(scopedKey), diagnostics);
    ext.exam = parseExamInfo(object.value(QStringLiteral("exam")), QStringLiteral("%1.exam").arg(scopedKey), diagnostics);

    ext.extra = parseExtraValue(object, QStringLiteral("extra"), scopedKey, diagnostics);
    mergeUnknownIntoExtra(ext.extra, collectUnknownFields(object, {"share", "relations", "exam", "extra"}));
    return ext;
}

bool parseRecord(const QString& rawTopLevelKey,
                 const QJsonValue& value,
                 CanonicalContentDiagnostics& diagnostics,
                 ConclusionRecord* outRecord,
                 QString* outRepositoryKey)
{
    if (outRecord == nullptr || outRepositoryKey == nullptr) {
        return false;
    }

    const QString trimmedKey = rawTopLevelKey.trimmed();
    if (trimmedKey.isEmpty()) {
        addWarning(diagnostics, QStringLiteral("root"), QStringLiteral("record key is empty and was skipped"));
        return false;
    }

    if (!value.isObject()) {
        addWarning(diagnostics,
                   buildScopeKey(trimmedKey, QStringLiteral("root")),
                   QStringLiteral("record value is not object and was skipped"));
        return false;
    }

    const QJsonObject object = value.toObject();
    if (object.isEmpty()) {
        addWarning(diagnostics, buildScopeKey(trimmedKey, QStringLiteral("root")), QStringLiteral("record object is empty"));
    }

    ConclusionRecord record;
    record.id = readString(object, QStringLiteral("id"), buildScopeKey(trimmedKey, QStringLiteral("id")), diagnostics).trimmed();
    if (record.id.isEmpty()) {
        // Repository key always follows the top-level map key. Backfill record.id when source id is missing.
        record.id = trimmedKey;
        addWarning(diagnostics,
                   buildScopeKey(trimmedKey, QStringLiteral("id")),
                   QStringLiteral("record.id is empty, fallback to top-level key '%1'").arg(trimmedKey));
    } else if (record.id != trimmedKey) {
        addWarning(diagnostics,
                   buildScopeKey(trimmedKey, QStringLiteral("id")),
                   QStringLiteral("record.id '%1' mismatches top-level key '%2'; repository key takes top-level key")
                       .arg(record.id, trimmedKey));
        record.id = trimmedKey;
    }

    const QString scopeRoot = buildScopeKey(trimmedKey, QStringLiteral("record"));
    record.schemaVersion = readInt(object, QStringLiteral("schema_version"), scopeRoot, diagnostics);
    record.type = readString(object, QStringLiteral("type"), scopeRoot, diagnostics);
    record.status = readString(object, QStringLiteral("status"), scopeRoot, diagnostics);

    if (!object.value(QStringLiteral("identity")).isObject()) {
        addWarning(diagnostics, scopeRoot, QStringLiteral("identity is missing or invalid, record skipped"));
        return false;
    }
    if (!object.value(QStringLiteral("meta")).isObject()) {
        addWarning(diagnostics, scopeRoot, QStringLiteral("meta is missing or invalid, record skipped"));
        return false;
    }
    if (!object.value(QStringLiteral("content")).isObject()) {
        addWarning(diagnostics, scopeRoot, QStringLiteral("content is missing or invalid, record skipped"));
        return false;
    }

    record.identity =
        parseIdentity(object.value(QStringLiteral("identity")).toObject(), buildScopeKey(trimmedKey, QStringLiteral("identity")), diagnostics);
    record.meta = parseMeta(object.value(QStringLiteral("meta")).toObject(), buildScopeKey(trimmedKey, QStringLiteral("meta")), diagnostics);
    record.content =
        parseContent(object.value(QStringLiteral("content")).toObject(), buildScopeKey(trimmedKey, QStringLiteral("content")), diagnostics);
    record.assets = parseAssets(object.value(QStringLiteral("assets")), buildScopeKey(trimmedKey, QStringLiteral("assets")), diagnostics);
    record.ext = parseExt(object.value(QStringLiteral("ext")), buildScopeKey(trimmedKey, QStringLiteral("ext")), diagnostics);

    record.extra = parseExtraValue(object, QStringLiteral("extra"), scopeRoot, diagnostics);
    mergeUnknownIntoExtra(
        record.extra,
        collectUnknownFields(object, {"id", "schema_version", "type", "status", "identity", "meta", "content", "assets", "ext", "extra"}));

    *outRecord = std::move(record);
    *outRepositoryKey = trimmedKey;
    return true;
}

QString resolveDefaultCanonicalPath()
{
    QStringList roots;
    roots << QDir::currentPath();
    if (QCoreApplication::instance() != nullptr) {
        roots << QCoreApplication::applicationDirPath();
    }

    for (const QString& root : std::as_const(roots)) {
        QDir probe(root);
        for (int depth = 0; depth <= 8; ++depth) {
            const QString candidate = probe.filePath(QString::fromUtf8(kDefaultRelativePath));
            if (QFileInfo::exists(candidate)) {
                return QFileInfo(candidate).absoluteFilePath();
            }
            if (!probe.cdUp()) {
                break;
            }
        }
    }

    // Development convention fallback: project_root/data/canonical_content_v2.json relative to current working dir.
    return QDir(QDir::currentPath()).filePath(QString::fromUtf8(kDefaultRelativePath));
}

}  // namespace

bool CanonicalContentDiagnostics::isSuccess() const
{
    return fatalError.isEmpty();
}

bool CanonicalContentLoadResult::isSuccess() const
{
    return diagnostics.isSuccess();
}

QString CanonicalContentLoader::defaultCanonicalContentPath()
{
    return resolveDefaultCanonicalPath();
}

CanonicalContentLoadResult CanonicalContentLoader::loadFromFile(const QString& filePath)
{
    CanonicalContentLoadResult result;
    const QString resolvedPath = filePath.trimmed().isEmpty() ? resolveDefaultCanonicalPath() : filePath.trimmed();

    QFileInfo fileInfo(resolvedPath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        result.diagnostics.fatalError = QStringLiteral("canonical content file not found: %1").arg(resolvedPath);
        return result;
    }

    QFile file(resolvedPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.diagnostics.fatalError =
            QStringLiteral("failed to open canonical content file: %1, reason=%2").arg(resolvedPath, file.errorString());
        return result;
    }

    const QByteArray rawBytes = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(rawBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        result.diagnostics.fatalError = QStringLiteral("failed to parse canonical content JSON: %1 (offset=%2)")
                                            .arg(parseError.errorString())
                                            .arg(parseError.offset);
        return result;
    }

    if (!document.isObject()) {
        result.diagnostics.fatalError = QStringLiteral("invalid canonical content JSON: root must be an object/map");
        return result;
    }

    const QJsonObject root = document.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        ConclusionRecord parsedRecord;
        QString repositoryKey;
        const bool recordOk = parseRecord(it.key(), it.value(), result.diagnostics, &parsedRecord, &repositoryKey);
        if (!recordOk) {
            result.diagnostics.skippedRecordIds.push_back(it.key());
            continue;
        }

        if (result.recordsById.contains(repositoryKey)) {
            addWarning(result.diagnostics,
                       buildScopeKey(repositoryKey, QStringLiteral("root")),
                       QStringLiteral("duplicate repository key encountered, later record overrides earlier one"));
        }
        result.recordsById.insert(repositoryKey, std::move(parsedRecord));
    }

    result.diagnostics.loadedCount = result.recordsById.size();
    result.diagnostics.skippedCount = result.diagnostics.skippedRecordIds.size();

    if (result.diagnostics.loadedCount == 0) {
        result.diagnostics.fatalError = QStringLiteral(
            "canonical content load failed: loaded_count=0 after parsing (skipped_count=%1, path=%2)")
                                            .arg(result.diagnostics.skippedCount)
                                            .arg(resolvedPath);
    }

    return result;
}

}  // namespace infrastructure::data

