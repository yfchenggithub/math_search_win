#include "domain/adapters/conclusion_card_adapter.h"

#include <QSet>

namespace domain::adapters {
namespace {

QString pickSummaryPlain(const models::ConclusionRecord& record)
{
    // Card summary prioritizes editorial summary, then plain fallback fields for resilience.
    if (!record.meta.summary.trimmed().isEmpty()) {
        return record.meta.summary.trimmed();
    }
    if (!record.content.plain.summary.trimmed().isEmpty()) {
        return record.content.plain.summary.trimmed();
    }
    if (!record.content.plain.statement.trimmed().isEmpty()) {
        return record.content.plain.statement.trimmed();
    }
    return {};
}

QString pickFormulaFallbackText(const models::ConclusionRecord& record)
{
    if (!record.content.primaryFormula.trimmed().isEmpty()) {
        return record.content.primaryFormula.trimmed();
    }

    // Fallback reads structured section "core_formula" first, because sections are the canonical render source.
    for (const models::RenderSection& section : record.content.sections) {
        if (section.key.compare(QStringLiteral("core_formula"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        for (const models::RenderBlock& block : section.blocks) {
            if (block.type.compare(QStringLiteral("math_block"), Qt::CaseInsensitive) == 0
                && !block.latex.trimmed().isEmpty()) {
                return block.latex.trimmed();
            }
        }
        for (const models::RenderBlock& block : section.blocks) {
            if (!block.latex.trimmed().isEmpty()) {
                return block.latex.trimmed();
            }
        }
    }
    return {};
}

QStringList buildSearchKeywords(const models::ConclusionRecord& record)
{
    QStringList keywords;
    QSet<QString> dedupe;

    const auto addKeyword = [&keywords, &dedupe](const QString& value) {
        const QString normalized = value.trimmed();
        if (normalized.isEmpty() || dedupe.contains(normalized)) {
            return;
        }
        dedupe.insert(normalized);
        keywords.push_back(normalized);
    };

    addKeyword(record.meta.title);
    addKeyword(record.identity.module);
    addKeyword(record.meta.category);
    for (const QString& alias : record.meta.aliases) {
        addKeyword(alias);
    }
    for (const QString& tag : record.meta.tags) {
        addKeyword(tag);
    }
    return keywords;
}

}  // namespace

ConclusionCardViewData ConclusionCardAdapter::toViewData(const models::ConclusionRecord& record)
{
    ConclusionCardViewData viewData;
    viewData.id = record.id;
    viewData.title = record.meta.title;
    viewData.module = record.identity.module;
    viewData.category = record.meta.category;
    viewData.difficulty = record.meta.difficulty;
    viewData.tags = record.meta.tags;
    viewData.summaryPlain = pickSummaryPlain(record);
    viewData.formulaFallbackText = pickFormulaFallbackText(record);
    viewData.formulaSvgPath = QString();
    viewData.isPro = record.meta.isPro;
    viewData.assetSvgName = record.assets.svg;
    viewData.searchKeywords = buildSearchKeywords(record);
    viewData.aliases = record.meta.aliases;
    return viewData;
}

}  // namespace domain::adapters

