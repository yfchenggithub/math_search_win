#include "domain/adapters/conclusion_detail_adapter.h"

namespace domain::adapters {
namespace {

QString pickSummary(const models::ConclusionRecord& record)
{
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

QString pickPrimaryFormula(const models::ConclusionRecord& record)
{
    if (!record.content.primaryFormula.trimmed().isEmpty()) {
        return record.content.primaryFormula.trimmed();
    }

    for (const models::RenderSection& section : record.content.sections) {
        if (section.key.compare(QStringLiteral("core_formula"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        for (const models::RenderBlock& block : section.blocks) {
            if (!block.latex.trimmed().isEmpty()) {
                return block.latex.trimmed();
            }
        }
    }
    return {};
}

QVector<DetailSectionViewData> buildSectionViews(const models::ConclusionRecord& record)
{
    QVector<DetailSectionViewData> sectionViews;
    sectionViews.reserve(record.content.sections.size());

    for (const models::RenderSection& section : record.content.sections) {
        DetailSectionViewData sectionView;
        sectionView.key = section.key;
        sectionView.title = section.title;
        sectionView.blockType = section.blockType;
        sectionView.sourceSection = &section;

        sectionView.blocks.reserve(section.blocks.size());
        for (const models::RenderBlock& block : section.blocks) {
            DetailBlockViewData blockView;
            blockView.type = block.type;
            blockView.knownType = block.knownType;
            blockView.sourceBlock = &block;
            sectionView.blocks.push_back(blockView);
        }

        sectionViews.push_back(std::move(sectionView));
    }

    return sectionViews;
}

}  // namespace

ConclusionDetailViewData ConclusionDetailAdapter::toViewData(const models::ConclusionRecord& record)
{
    ConclusionDetailViewData viewData;
    viewData.id = record.id;
    viewData.title = record.meta.title;
    viewData.module = record.identity.module;
    viewData.knowledgeNode = record.identity.knowledgeNode;
    viewData.altNodes = record.identity.altNodes;
    viewData.difficulty = record.meta.difficulty;
    viewData.category = record.meta.category;
    viewData.tags = record.meta.tags;
    viewData.aliases = record.meta.aliases;
    viewData.summary = pickSummary(record);
    viewData.primaryFormula = pickPrimaryFormula(record);

    viewData.variables = &record.content.variables;
    viewData.conditions = &record.content.conditions;
    viewData.conclusions = &record.content.conclusions;
    viewData.rawSections = &record.content.sections;
    viewData.sections = buildSectionViews(record);

    // plain_* fields stay flattened for fallback rendering paths when structured sections are incomplete.
    viewData.plainStatement = record.content.plain.statement;
    viewData.plainExplanation = record.content.plain.explanation;
    viewData.plainProof = record.content.plain.proof;
    viewData.plainExamples = record.content.plain.examples;
    viewData.plainTraps = record.content.plain.traps;
    viewData.plainSummary = record.content.plain.summary;

    viewData.assets = &record.assets;
    viewData.relations = &record.ext.relations;
    viewData.shareInfo = &record.ext.share;
    viewData.examInfo = &record.ext.exam;
    viewData.isPro = record.meta.isPro;
    return viewData;
}

}  // namespace domain::adapters

