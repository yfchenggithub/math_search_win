#include "domain/adapters/conclusion_detail_adapter.h"

#include <QHash>
#include <QStringList>

namespace domain::adapters {
namespace {

QString firstNonEmpty(const QStringList& values)
{
    for (const QString& rawValue : values) {
        const QString value = rawValue.trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

QString normalizeKey(const QString& key)
{
    return key.trimmed().toLower();
}

QString mapSourceSectionKeyToTarget(const QString& sourceKey)
{
    const QString key = normalizeKey(sourceKey);
    if (key == QStringLiteral("statement") || key == QStringLiteral("core_formula")) {
        return QStringLiteral("statement");
    }
    if (key == QStringLiteral("conditions") || key == QStringLiteral("condition")) {
        return QStringLiteral("condition");
    }
    if (key == QStringLiteral("remarks") || key == QStringLiteral("remark")) {
        return QStringLiteral("remarks");
    }
    if (key == QStringLiteral("intuition") || key == QStringLiteral("explanation")) {
        return QStringLiteral("intuition");
    }
    if (key == QStringLiteral("derivation")) {
        return QStringLiteral("derivation");
    }
    if (key == QStringLiteral("proof")) {
        return QStringLiteral("proof");
    }
    if (key == QStringLiteral("usage") || key == QStringLiteral("examples")) {
        return QStringLiteral("usage");
    }
    if (key == QStringLiteral("pitfalls") || key == QStringLiteral("traps")) {
        return QStringLiteral("pitfalls");
    }
    if (key == QStringLiteral("summary")) {
        return QStringLiteral("summary");
    }
    if (key == QStringLiteral("variables") || key == QStringLiteral("variable") || key == QStringLiteral("vars")) {
        return QStringLiteral("vars");
    }
    if (key == QStringLiteral("notes") || key == QStringLiteral("note")) {
        return QStringLiteral("notes");
    }
    return QStringLiteral("notes");
}

QString sectionTitleForKey(const QString& targetKey)
{
    if (targetKey == QStringLiteral("statement")) {
        return QStringLiteral("结论");
    }
    if (targetKey == QStringLiteral("condition")) {
        return QStringLiteral("条件");
    }
    if (targetKey == QStringLiteral("remarks")) {
        return QStringLiteral("备注");
    }
    if (targetKey == QStringLiteral("intuition")) {
        return QStringLiteral("理解");
    }
    if (targetKey == QStringLiteral("derivation")) {
        return QStringLiteral("推导");
    }
    if (targetKey == QStringLiteral("proof")) {
        return QStringLiteral("证明");
    }
    if (targetKey == QStringLiteral("usage")) {
        return QStringLiteral("用法");
    }
    if (targetKey == QStringLiteral("pitfalls")) {
        return QStringLiteral("易错点");
    }
    if (targetKey == QStringLiteral("summary")) {
        return QStringLiteral("总结");
    }
    if (targetKey == QStringLiteral("vars")) {
        return QStringLiteral("变量");
    }
    if (targetKey == QStringLiteral("notes")) {
        return QStringLiteral("补充说明");
    }
    return QStringLiteral("内容");
}

QString sectionKindForKey(const QString& targetKey)
{
    if (targetKey == QStringLiteral("pitfalls")) {
        return QStringLiteral("pitfall");
    }
    if (targetKey == QStringLiteral("summary")) {
        return QStringLiteral("summary");
    }
    if (targetKey == QStringLiteral("remarks") || targetKey == QStringLiteral("notes") || targetKey == QStringLiteral("vars")) {
        return QStringLiteral("note");
    }
    return QStringLiteral("normal");
}

QString normalizeSectionKind(const QString& kind)
{
    const QString normalized = kind.trimmed().toLower();
    if (normalized == QStringLiteral("note") || normalized == QStringLiteral("pitfall") || normalized == QStringLiteral("summary")
        || normalized == QStringLiteral("normal")) {
        return normalized;
    }
    return QStringLiteral("normal");
}

QString renderTextWithLineBreaks(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    return trimmed.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
}

QString renderTokenToHtml(const models::RenderToken& token)
{
    if (token.knownType == models::RenderTokenKnownType::MathInline && !token.latex.trimmed().isEmpty()) {
        return QStringLiteral("<span class=\"detail-math-inline\">\\(%1\\)</span>").arg(token.latex.trimmed().toHtmlEscaped());
    }

    if (!token.text.trimmed().isEmpty()) {
        return renderTextWithLineBreaks(token.text);
    }

    if (!token.latex.trimmed().isEmpty()) {
        return QStringLiteral("<span class=\"detail-math-inline\">\\(%1\\)</span>").arg(token.latex.trimmed().toHtmlEscaped());
    }

    return {};
}

QString renderTokenListToHtml(const QVector<models::RenderToken>& tokens)
{
    QString rendered;
    rendered.reserve(tokens.size() * 24);
    for (const models::RenderToken& token : tokens) {
        rendered += renderTokenToHtml(token);
    }
    return rendered.trimmed();
}

QString renderTheoremGroupItemToHtml(const models::TheoremGroupItem& item)
{
    QStringList htmlParts;

    const QString itemTitle = item.title.trimmed();
    if (!itemTitle.isEmpty()) {
        htmlParts.push_back(QStringLiteral("<p><strong>%1</strong></p>").arg(itemTitle.toHtmlEscaped()));
    }

    const QString formula = item.formulaLatex.trimmed();
    if (!formula.isEmpty()) {
        htmlParts.push_back(QStringLiteral("<div class=\"detail-math-block\">$$%1$$</div>").arg(formula.toHtmlEscaped()));
    }

    const QString desc = renderTokenListToHtml(item.descTokens);
    if (!desc.isEmpty()) {
        htmlParts.push_back(QStringLiteral("<p>%1</p>").arg(desc));
    }

    return htmlParts.join(QString());
}

QString renderBlockToHtml(const models::RenderBlock& block)
{
    if (block.knownType == models::RenderBlockKnownType::MathBlock || normalizeKey(block.type) == QStringLiteral("math_block")) {
        const QString latex = block.latex.trimmed();
        if (latex.isEmpty()) {
            return {};
        }
        return QStringLiteral("<div class=\"detail-math-block\">$$%1$$</div>").arg(latex.toHtmlEscaped());
    }

    if (block.knownType == models::RenderBlockKnownType::TheoremGroup || normalizeKey(block.type) == QStringLiteral("theorem_group")) {
        QStringList htmlParts;
        for (const models::TheoremGroupItem& item : block.items) {
            const QString itemHtml = renderTheoremGroupItemToHtml(item);
            if (!itemHtml.isEmpty()) {
                htmlParts.push_back(itemHtml);
            }
        }
        return htmlParts.join(QString());
    }

    const QString tokenHtml = renderTokenListToHtml(block.tokens);
    if (!tokenHtml.isEmpty()) {
        return QStringLiteral("<p>%1</p>").arg(tokenHtml);
    }

    if (!block.latex.trimmed().isEmpty()) {
        return QStringLiteral("<div class=\"detail-math-block\">$$%1$$</div>").arg(block.latex.trimmed().toHtmlEscaped());
    }

    return {};
}

QString renderSectionBlocksToHtml(const models::RenderSection& section)
{
    QStringList blockHtmlList;
    blockHtmlList.reserve(section.blocks.size());
    for (const models::RenderBlock& block : section.blocks) {
        const QString blockHtml = renderBlockToHtml(block).trimmed();
        if (!blockHtml.isEmpty()) {
            blockHtmlList.push_back(blockHtml);
        }
    }
    return blockHtmlList.join(QString());
}

QStringList trimAndFilterList(const QStringList& input)
{
    QStringList output;
    output.reserve(input.size());
    for (const QString& rawValue : input) {
        const QString value = rawValue.trimmed();
        if (!value.isEmpty()) {
            output.push_back(value);
        }
    }
    return output;
}

QString renderConditionFragmentToText(const models::ContentFragment& fragment)
{
    const QString text = fragment.text.trimmed();
    if (!text.isEmpty()) {
        return text;
    }
    const QString latex = fragment.latex.trimmed();
    if (!latex.isEmpty()) {
        return QStringLiteral("\\(%1\\)").arg(latex);
    }
    return {};
}

QString renderConditionContentToText(const QVector<models::ContentFragment>& fragments)
{
    QStringList chunks;
    chunks.reserve(fragments.size());
    for (const models::ContentFragment& fragment : fragments) {
        const QString rendered = renderConditionFragmentToText(fragment);
        if (!rendered.isEmpty()) {
            chunks.push_back(rendered);
        }
    }
    return chunks.join(QStringLiteral(" "));
}

QString buildConditionFallbackText(const models::ConclusionRecord& record)
{
    QStringList lines;
    lines.reserve(record.content.conditions.size());
    for (const models::ConditionDef& condition : record.content.conditions) {
        const QString title = condition.title.trimmed();
        const QString contentText = renderConditionContentToText(condition.content).trimmed();
        if (title.isEmpty() && contentText.isEmpty()) {
            continue;
        }
        if (!title.isEmpty() && !contentText.isEmpty()) {
            lines.push_back(QStringLiteral("%1：%2").arg(title, contentText));
            continue;
        }
        lines.push_back(title.isEmpty() ? contentText : title);
    }
    return lines.join(QStringLiteral("\n"));
}

QVector<DetailVariableViewData> buildVariables(const models::ConclusionRecord& record)
{
    QVector<DetailVariableViewData> variables;
    variables.reserve(record.content.variables.size());
    for (const models::VariableDef& source : record.content.variables) {
        DetailVariableViewData row;
        row.name = source.name.trimmed();
        row.latex = source.latex.trimmed();
        row.description = source.description.trimmed();
        row.required = source.required;
        if (row.name.isEmpty() && row.latex.isEmpty() && row.description.isEmpty()) {
            continue;
        }
        variables.push_back(std::move(row));
    }
    return variables;
}

QString variablesToFallbackText(const QVector<DetailVariableViewData>& variables)
{
    QStringList lines;
    lines.reserve(variables.size());
    for (const DetailVariableViewData& variable : variables) {
        const QString name = firstNonEmpty({variable.name, variable.latex});
        const QString desc = variable.description.trimmed();
        if (name.isEmpty() && desc.isEmpty()) {
            continue;
        }
        if (!name.isEmpty() && !desc.isEmpty()) {
            lines.push_back(QStringLiteral("%1：%2").arg(name, desc));
            continue;
        }
        lines.push_back(name.isEmpty() ? desc : name);
    }
    return lines.join(QStringLiteral("\n"));
}

struct SectionAccumulator {
    QString key;
    QString title;
    QString kind;
    QStringList htmlParts;
    QStringList textParts;
};

QVector<DetailSectionViewData> buildSections(const models::ConclusionRecord& record,
                                             const QString& pageSummary,
                                             const QString& conditionFallbackText,
                                             const QString& remarkFallbackText,
                                             const QVector<DetailVariableViewData>& variables)
{
    const QStringList orderedKeys = {
        QStringLiteral("statement"),
        QStringLiteral("condition"),
        QStringLiteral("remarks"),
        QStringLiteral("intuition"),
        QStringLiteral("derivation"),
        QStringLiteral("proof"),
        QStringLiteral("usage"),
        QStringLiteral("pitfalls"),
        QStringLiteral("summary"),
        QStringLiteral("vars"),
        QStringLiteral("notes"),
    };

    QHash<QString, SectionAccumulator> accumulators;
    for (const QString& key : orderedKeys) {
        SectionAccumulator accumulator;
        accumulator.key = key;
        accumulator.title = sectionTitleForKey(key);
        accumulator.kind = sectionKindForKey(key);
        accumulators.insert(key, std::move(accumulator));
    }

    for (const models::RenderSection& rawSection : record.content.sections) {
        const QString targetKey = mapSourceSectionKeyToTarget(rawSection.key);
        auto it = accumulators.find(targetKey);
        if (it == accumulators.end()) {
            continue;
        }

        const QString sectionHtml = renderSectionBlocksToHtml(rawSection).trimmed();
        if (sectionHtml.isEmpty()) {
            continue;
        }

        const bool mappedFromAnotherKey = normalizeKey(rawSection.key) != targetKey;
        const QString sourceTitle = rawSection.title.trimmed();
        if (mappedFromAnotherKey && !sourceTitle.isEmpty()
            && (targetKey == QStringLiteral("notes") || targetKey == QStringLiteral("remarks")
                || targetKey == QStringLiteral("condition"))) {
            it->htmlParts.push_back(QStringLiteral("<p><strong>%1</strong></p>").arg(sourceTitle.toHtmlEscaped()));
        }
        it->htmlParts.push_back(sectionHtml);
    }

    auto appendTextFallback = [&accumulators](const QString& key, const QString& text) {
        auto it = accumulators.find(key);
        if (it == accumulators.end()) {
            return;
        }
        const QString normalized = text.trimmed();
        if (!normalized.isEmpty()) {
            it->textParts.push_back(normalized);
        }
    };

    appendTextFallback(QStringLiteral("statement"), record.content.plain.statement);
    appendTextFallback(QStringLiteral("condition"), conditionFallbackText);
    appendTextFallback(QStringLiteral("remarks"), remarkFallbackText);
    appendTextFallback(QStringLiteral("intuition"), record.content.plain.explanation);
    appendTextFallback(QStringLiteral("proof"), record.content.plain.proof);
    appendTextFallback(QStringLiteral("usage"), record.content.plain.examples);
    appendTextFallback(QStringLiteral("pitfalls"), record.content.plain.traps);
    appendTextFallback(QStringLiteral("vars"), variablesToFallbackText(variables));
    if (record.content.plain.summary.trimmed() != pageSummary.trimmed()) {
        appendTextFallback(QStringLiteral("summary"), record.content.plain.summary);
    }

    QVector<DetailSectionViewData> sections;
    sections.reserve(orderedKeys.size());
    for (const QString& key : orderedKeys) {
        const auto it = accumulators.constFind(key);
        if (it == accumulators.constEnd()) {
            continue;
        }

        DetailSectionViewData section;
        section.key = it->key;
        section.title = it->title;
        section.kind = normalizeSectionKind(it->kind);
        section.html = it->htmlParts.join(QString()).trimmed();
        section.text = it->textParts.join(QStringLiteral("\n\n")).trimmed();
        section.visible = !(section.html.isEmpty() && section.text.isEmpty());
        if (section.visible) {
            sections.push_back(std::move(section));
        }
    }

    return sections;
}

}  // namespace

ConclusionDetailViewData ConclusionDetailAdapter::toViewData(const models::ConclusionRecord& record)
{
    ConclusionDetailViewData viewData;
    viewData.title = firstNonEmpty({record.meta.title, record.id, QStringLiteral("未命名结论")});
    viewData.conclusionId = record.id.trimmed();
    viewData.module = firstNonEmpty({record.identity.module, QStringLiteral("未知模块")});
    viewData.moduleKey = record.identity.module.trimmed();
    viewData.category = record.meta.category.trimmed();
    viewData.tags = trimAndFilterList(record.meta.tags);
    viewData.summary = firstNonEmpty({record.meta.summary, record.content.plain.summary, record.content.plain.statement});
    viewData.conditionText = buildConditionFallbackText(record);
    viewData.remarkText = record.meta.remarks.trimmed();
    viewData.variables = buildVariables(record);
    viewData.sections = buildSections(record, viewData.summary, viewData.conditionText, viewData.remarkText, viewData.variables);
    viewData.isValid = !viewData.title.trimmed().isEmpty()
                       && (!viewData.summary.trimmed().isEmpty() || !viewData.sections.isEmpty()
                           || !viewData.tags.isEmpty() || !viewData.conclusionId.trimmed().isEmpty());
    if (!viewData.isValid) {
        viewData.errorMessage = QStringLiteral("详情数据暂时不可用。");
    }
    return viewData;
}

}  // namespace domain::adapters
