#include "ui/detail/detail_view_data_mapper.h"

#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStringList>

namespace ui::detail {
namespace {

QString normalizedKey(const QString& key)
{
    return key.trimmed().toLower();
}

QString plainTextToHtml(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    const QStringList paragraphs = trimmed.split(QRegularExpression(QStringLiteral(R"(\n{2,})")), Qt::SkipEmptyParts);
    QStringList htmlParts;
    htmlParts.reserve(paragraphs.size());
    for (const QString& paragraph : paragraphs) {
        const QString normalizedParagraph = paragraph.trimmed();
        if (normalizedParagraph.isEmpty()) {
            continue;
        }
        htmlParts.push_back(QStringLiteral("<p>%1</p>")
                                .arg(normalizedParagraph.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br/>"))));
    }

    return htmlParts.join(QString());
}

QString sectionToHtml(const domain::adapters::DetailSectionViewData& section)
{
    const QString html = section.html.trimmed();
    if (!html.isEmpty()) {
        return html;
    }
    return plainTextToHtml(section.text);
}

const domain::adapters::DetailSectionViewData* findSection(const QVector<domain::adapters::DetailSectionViewData>& sections,
                                                           const QStringList& keys)
{
    for (const QString& key : keys) {
        const QString normalized = normalizedKey(key);
        for (const domain::adapters::DetailSectionViewData& section : sections) {
            if (!section.visible) {
                continue;
            }
            if (normalizedKey(section.key) == normalized) {
                return &section;
            }
        }
    }
    return nullptr;
}

QString sectionLabelFromId(const QString& sectionId)
{
    if (sectionId == QStringLiteral("proof")) {
        return QStringLiteral("证明");
    }
    if (sectionId == QStringLiteral("pitfalls")) {
        return QStringLiteral("易错点");
    }
    if (sectionId == QStringLiteral("usage")) {
        return QStringLiteral("用法");
    }
    if (sectionId == QStringLiteral("summary")) {
        return QStringLiteral("总结");
    }
    if (sectionId == QStringLiteral("intuition")) {
        return QStringLiteral("理解");
    }
    if (sectionId == QStringLiteral("derivation")) {
        return QStringLiteral("推导");
    }
    if (sectionId == QStringLiteral("notes")) {
        return QStringLiteral("备注");
    }
    return QStringLiteral("内容");
}

QString normalizeSectionId(const QString& rawId)
{
    const QString key = normalizedKey(rawId);
    if (key == QStringLiteral("pitfall")) {
        return QStringLiteral("pitfalls");
    }
    if (key == QStringLiteral("note")) {
        return QStringLiteral("notes");
    }
    return key.isEmpty() ? QStringLiteral("notes") : key;
}

QString sectionPriority(const QString& sectionId)
{
    if (sectionId == QStringLiteral("proof") || sectionId == QStringLiteral("pitfalls") || sectionId == QStringLiteral("usage")
        || sectionId == QStringLiteral("summary") || sectionId == QStringLiteral("intuition")
        || sectionId == QStringLiteral("derivation") || sectionId == QStringLiteral("notes")) {
        return QStringLiteral("heavy");
    }
    return QStringLiteral("light");
}

}  // namespace

QJsonObject DetailViewDataMapper::buildContentPayload(const domain::adapters::ConclusionDetailViewData& detail,
                                                      quint64 requestId) const
{
    QJsonObject payload;
    payload.insert(QStringLiteral("state"), QStringLiteral("content"));
    payload.insert(QStringLiteral("requestId"), static_cast<qint64>(requestId));
    payload.insert(QStringLiteral("detailId"), detail.conclusionId.trimmed());
    payload.insert(QStringLiteral("title"), detail.title.trimmed());
    payload.insert(QStringLiteral("module"), detail.module.trimmed());
    payload.insert(QStringLiteral("category"), detail.category.trimmed());

    QJsonArray tagsJson;
    for (const QString& tag : detail.tags) {
        const QString trimmedTag = tag.trimmed();
        if (!trimmedTag.isEmpty()) {
            tagsJson.append(trimmedTag);
        }
    }
    payload.insert(QStringLiteral("tags"), tagsJson);

    const auto* statementSection = findSection(detail.sections, {QStringLiteral("statement")});
    const auto* conditionSection = findSection(detail.sections, {QStringLiteral("condition"), QStringLiteral("conditions")});
    const auto* remarksSection = findSection(detail.sections, {QStringLiteral("remarks"), QStringLiteral("notes")});

    QString statementHtml = statementSection == nullptr ? QString() : sectionToHtml(*statementSection);
    if (statementHtml.trimmed().isEmpty()) {
        statementHtml = plainTextToHtml(detail.summary);
    }

    QString conditionHtml = plainTextToHtml(detail.conditionText);
    if (conditionHtml.trimmed().isEmpty() && conditionSection != nullptr) {
        conditionHtml = sectionToHtml(*conditionSection);
    }

    QString remarkHtml = plainTextToHtml(detail.remarkText);
    if (remarkHtml.trimmed().isEmpty() && remarksSection != nullptr) {
        remarkHtml = sectionToHtml(*remarksSection);
    }

    payload.insert(QStringLiteral("statementHtml"), statementHtml);
    payload.insert(QStringLiteral("coreHtml"), statementHtml);
    payload.insert(QStringLiteral("conditionHtml"), conditionHtml);
    payload.insert(QStringLiteral("remarkHtml"), remarkHtml);

    QJsonArray varsJson;
    for (const domain::adapters::DetailVariableViewData& variable : detail.variables) {
        QJsonObject row;
        row.insert(QStringLiteral("name"), variable.name.trimmed());
        row.insert(QStringLiteral("latex"), variable.latex.trimmed());
        row.insert(QStringLiteral("description"), variable.description.trimmed());
        row.insert(QStringLiteral("required"), variable.required);
        if (row.value(QStringLiteral("name")).toString().isEmpty() && row.value(QStringLiteral("latex")).toString().isEmpty()
            && row.value(QStringLiteral("description")).toString().isEmpty()) {
            continue;
        }
        varsJson.append(row);
    }
    payload.insert(QStringLiteral("vars"), varsJson);

    QHash<QString, int> sectionIdCounts;
    QJsonArray sectionsJson;
    for (const domain::adapters::DetailSectionViewData& section : detail.sections) {
        if (!section.visible) {
            continue;
        }

        const QString html = sectionToHtml(section).trimmed();
        if (html.isEmpty()) {
            continue;
        }

        QString sectionId = normalizeSectionId(section.key);
        if (sectionId == QStringLiteral("statement") || sectionId == QStringLiteral("condition")
            || sectionId == QStringLiteral("conditions") || sectionId == QStringLiteral("remarks")
            || sectionId == QStringLiteral("vars")) {
            continue;
        }

        const int currentCount = sectionIdCounts.value(sectionId, 0);
        sectionIdCounts.insert(sectionId, currentCount + 1);
        if (currentCount > 0) {
            sectionId = QStringLiteral("%1_%2").arg(sectionId).arg(currentCount + 1);
        }

        const QString canonicalSectionId = sectionId.split(QLatin1Char('_')).first();

        QJsonObject sectionJson;
        sectionJson.insert(QStringLiteral("id"), sectionId);
        sectionJson.insert(QStringLiteral("label"),
                           section.title.trimmed().isEmpty() ? sectionLabelFromId(canonicalSectionId)
                                                             : section.title.trimmed());
        sectionJson.insert(QStringLiteral("priority"), sectionPriority(canonicalSectionId));
        sectionJson.insert(QStringLiteral("html"), html);
        sectionsJson.append(sectionJson);
    }
    payload.insert(QStringLiteral("sections"), sectionsJson);

    return payload;
}

QJsonObject DetailViewDataMapper::buildEmptyPayload(const QString& message) const
{
    QJsonObject payload;
    payload.insert(QStringLiteral("state"), QStringLiteral("empty"));
    payload.insert(QStringLiteral("requestId"), 0);
    payload.insert(QStringLiteral("detailId"), QString());
    payload.insert(QStringLiteral("message"),
                   message.trimmed().isEmpty() ? QStringLiteral("请选择左侧结果查看详情。") : message.trimmed());
    payload.insert(QStringLiteral("sections"), QJsonArray());
    payload.insert(QStringLiteral("vars"), QJsonArray());
    return payload;
}

QJsonObject DetailViewDataMapper::buildErrorPayload(const QString& message,
                                                    const QString& detailId,
                                                    quint64 requestId) const
{
    QJsonObject payload;
    payload.insert(QStringLiteral("state"), QStringLiteral("error"));
    payload.insert(QStringLiteral("requestId"), static_cast<qint64>(requestId));
    payload.insert(QStringLiteral("detailId"), detailId.trimmed());
    payload.insert(QStringLiteral("message"),
                   message.trimmed().isEmpty() ? QStringLiteral("详情暂时不可用。") : message.trimmed());
    payload.insert(QStringLiteral("sections"), QJsonArray());
    payload.insert(QStringLiteral("vars"), QJsonArray());
    return payload;
}

}  // namespace ui::detail
