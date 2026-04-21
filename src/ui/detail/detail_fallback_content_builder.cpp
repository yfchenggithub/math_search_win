#include "ui/detail/detail_fallback_content_builder.h"

#include <QRegularExpression>
#include <QStringList>

namespace ui::detail {
namespace {

QString toHtmlParagraph(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    return QStringLiteral("<p>%1</p>").arg(trimmed.toHtmlEscaped().replace('\n', QStringLiteral("<br/>")));
}

QString firstNonEmpty(const QStringList& values)
{
    for (const QString& value : values) {
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty()) {
            return trimmed;
        }
    }
    return {};
}

QString firstVisibleSectionSnippet(const QVector<domain::adapters::DetailSectionViewData>& sections)
{
    static const QRegularExpression kStripTagsPattern(QStringLiteral("<[^>]*>"));

    for (const domain::adapters::DetailSectionViewData& section : sections) {
        if (!section.visible) {
            continue;
        }

        const QString sectionText = section.text.trimmed();
        if (!sectionText.isEmpty()) {
            return sectionText;
        }

        const QString sectionHtml = section.html.trimmed();
        if (!sectionHtml.isEmpty()) {
            const QString stripped = QString(sectionHtml).remove(kStripTagsPattern).trimmed();
            if (!stripped.isEmpty()) {
                return stripped;
            }
        }
    }

    return {};
}

}  // namespace

QString DetailFallbackContentBuilder::buildFallbackHtml(const domain::adapters::ConclusionDetailViewData& detailView)
{
    QStringList html;
    html.push_back(QStringLiteral("<h2>%1</h2>").arg(detailView.title.trimmed().toHtmlEscaped()));
    html.push_back(QStringLiteral("<p><b>ID:</b> %1<br/><b>\u6a21\u5757:</b> %2</p>")
                       .arg(detailView.conclusionId.toHtmlEscaped(), detailView.module.toHtmlEscaped()));

    if (!detailView.tags.isEmpty()) {
        html.push_back(
            QStringLiteral("<p><b>\u6807\u7b7e:</b> %1</p>").arg(detailView.tags.join(QStringLiteral(" / ")).toHtmlEscaped()));
    }
    if (!detailView.summary.trimmed().isEmpty()) {
        html.push_back(QStringLiteral("<h3>\u6458\u8981</h3>"));
        html.push_back(toHtmlParagraph(detailView.summary));
    }

    if (!detailView.sections.isEmpty()) {
        html.push_back(QStringLiteral("<h3>\u6b63\u6587</h3>"));
        for (const domain::adapters::DetailSectionViewData& section : detailView.sections) {
            if (!section.visible) {
                continue;
            }
            const QString title = firstNonEmpty({section.title, section.key, QStringLiteral("\u5185\u5bb9")});
            html.push_back(QStringLiteral("<h4>%1</h4>").arg(title.toHtmlEscaped()));
            if (!section.html.trimmed().isEmpty()) {
                html.push_back(section.html);
            } else if (!section.text.trimmed().isEmpty()) {
                html.push_back(toHtmlParagraph(section.text));
            }
        }
    } else {
        html.push_back(QStringLiteral("<p style=\"color:#666;\">\u6682\u65e0\u66f4\u591a\u5185\u5bb9\u3002</p>"));
    }

    return html.join(QString());
}

QString DetailFallbackContentBuilder::buildTrialPreviewHtml(const domain::adapters::ConclusionDetailViewData& detailView,
                                                            const QString& docId,
                                                            const QString& previewDisabledReason,
                                                            int snippetLimit)
{
    const QString reason = previewDisabledReason.trimmed().isEmpty()
                               ? QStringLiteral("\u6b63\u5f0f\u7248\u89e3\u9501\u5b8c\u6574\u8be6\u60c5\u3002")
                               : previewDisabledReason.trimmed();
    const int safeSnippetLimit = snippetLimit <= 0 ? 220 : snippetLimit;

    QStringList html;
    html.push_back(QStringLiteral("<h2>%1</h2>").arg(detailView.title.trimmed().toHtmlEscaped()));
    html.push_back(QStringLiteral("<p><b>ID:</b> %1</p>").arg(docId.toHtmlEscaped()));

    const QString summary = detailView.summary.trimmed();
    if (!summary.isEmpty()) {
        html.push_back(QStringLiteral("<h3>\u6458\u8981</h3>"));
        html.push_back(toHtmlParagraph(summary));
    }

    QString snippet = firstVisibleSectionSnippet(detailView.sections);
    if (!snippet.isEmpty()) {
        if (snippet.size() > safeSnippetLimit) {
            snippet = snippet.left(safeSnippetLimit).trimmed();
            snippet.append(QStringLiteral("..."));
        }
        html.push_back(QStringLiteral("<h3>\u5185\u5bb9\u9884\u89c8</h3>"));
        html.push_back(toHtmlParagraph(snippet));
    }

    html.push_back(QStringLiteral("<p style=\"color:#9a3412;\"><b>%1</b></p>").arg(reason.toHtmlEscaped()));
    return html.join(QString());
}

}  // namespace ui::detail
