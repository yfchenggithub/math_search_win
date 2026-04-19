#include "ui/detail/detail_html_renderer.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"
#include "shared/paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

namespace ui::detail {
namespace {

QString ensureTrailingSlashLocalUrl(const QString& absoluteDirPath)
{
    QString path = QDir(absoluteDirPath).absolutePath();
    if (!path.endsWith(QLatin1Char('/'))) {
        path.append(QLatin1Char('/'));
    }
    return path;
}

DetailHtmlRenderResult makeFailure(const QString& message)
{
    DetailHtmlRenderResult result;
    result.success = false;
    result.errorMessage = message.trimmed().isEmpty() ? QStringLiteral("detail html render failed") : message.trimmed();
    return result;
}

QString normalizeSectionKind(const QString& rawKind)
{
    const QString kind = rawKind.trimmed().toLower();
    if (kind == QStringLiteral("note") || kind == QStringLiteral("pitfall") || kind == QStringLiteral("summary")
        || kind == QStringLiteral("normal")) {
        return kind;
    }
    return QStringLiteral("normal");
}

}  // namespace

DetailHtmlRenderer::DetailHtmlRenderer()
{
    const QString appRoot = AppPaths::appRoot();
    const QString resourcesRoot = QDir(appRoot).filePath(QStringLiteral("resources"));
    detailDirectory_ = QDir(resourcesRoot).filePath(QStringLiteral("detail"));
    templatePath_ = QDir(detailDirectory_).filePath(QStringLiteral("detail_template.html"));

    const QString katexCssPath = QDir(resourcesRoot).filePath(QStringLiteral("katex/katex.min.css"));
    const QString katexJsPath = QDir(resourcesRoot).filePath(QStringLiteral("katex/katex.min.js"));
    const QString katexAutoRenderPath = QDir(resourcesRoot).filePath(QStringLiteral("katex/contrib/auto-render.min.js"));
    const QString detailCssPath = QDir(detailDirectory_).filePath(QStringLiteral("detail.css"));
    const QString detailJsPath = QDir(detailDirectory_).filePath(QStringLiteral("detail.js"));

    LOG_INFO(LogCategory::WebViewKatex,
             QStringLiteral("detail renderer init appRoot=%1 detailDir=%2").arg(appRoot, detailDirectory_));

    const QStringList requiredFiles = {
        templatePath_,
        detailCssPath,
        detailJsPath,
        katexCssPath,
        katexJsPath,
        katexAutoRenderPath,
    };

    QStringList missingFiles;
    for (const QString& path : requiredFiles) {
        const QFileInfo info(path);
        if (!info.exists() || !info.isFile()) {
            missingFiles.push_back(path);
        } else {
            LOG_DEBUG(LogCategory::WebViewKatex, QStringLiteral("detail renderer resource ready path=%1").arg(path));
        }
    }

    if (!missingFiles.isEmpty()) {
        lastError_ = QStringLiteral("missing required detail resources: %1").arg(missingFiles.join(QStringLiteral(" | ")));
        LOG_WARN(LogCategory::WebViewKatex, lastError_);
        return;
    }

    QFile templateFile(templatePath_);
    if (!templateFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        lastError_ = QStringLiteral("failed to open detail template path=%1 reason=%2")
                         .arg(templatePath_, templateFile.errorString());
        LOG_ERROR(LogCategory::WebViewKatex, lastError_);
        return;
    }

    templateHtml_ = QString::fromUtf8(templateFile.readAll());
    templateFile.close();

    if (templateHtml_.trimmed().isEmpty()) {
        lastError_ = QStringLiteral("detail template is empty path=%1").arg(templatePath_);
        LOG_ERROR(LogCategory::WebViewKatex, lastError_);
        return;
    }

    if (!templateHtml_.contains(QStringLiteral("__DETAIL_PAYLOAD__"))) {
        lastError_ = QStringLiteral("detail template missing payload placeholder __DETAIL_PAYLOAD__ path=%1").arg(templatePath_);
        LOG_ERROR(LogCategory::WebViewKatex, lastError_);
        return;
    }

    baseUrl_ = QUrl::fromLocalFile(ensureTrailingSlashLocalUrl(detailDirectory_));
    ready_ = true;
    lastError_.clear();

    LOG_INFO(LogCategory::WebViewKatex,
             QStringLiteral("detail renderer ready template=%1 baseUrl=%2").arg(templatePath_, baseUrl_.toString()));
}

bool DetailHtmlRenderer::isReady() const
{
    return ready_;
}

QString DetailHtmlRenderer::lastError() const
{
    return lastError_;
}

QString DetailHtmlRenderer::detailDirectory() const
{
    return detailDirectory_;
}

DetailHtmlRenderResult DetailHtmlRenderer::renderEmptyState(const QString& message) const
{
    QJsonObject payload;
    payload.insert(QStringLiteral("state"), QStringLiteral("empty"));
    payload.insert(QStringLiteral("message"),
                   message.trimmed().isEmpty()
                       ? QStringLiteral("在左侧结果列表中点击一条结论，这里会显示详细内容。")
                       : message.trimmed());
    return renderPayload(payload);
}

DetailHtmlRenderResult DetailHtmlRenderer::renderErrorState(const QString& message) const
{
    QJsonObject payload;
    payload.insert(QStringLiteral("state"), QStringLiteral("error"));
    payload.insert(QStringLiteral("message"),
                   message.trimmed().isEmpty() ? QStringLiteral("详情暂时无法显示") : message.trimmed());
    return renderPayload(payload);
}

DetailHtmlRenderResult DetailHtmlRenderer::renderContent(const domain::adapters::ConclusionDetailViewData& detail) const
{
    if (!detail.isValid) {
        return renderErrorState(detail.errorMessage);
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("state"), QStringLiteral("content"));
    payload.insert(QStringLiteral("detail"), toJson(detail));
    return renderPayload(payload);
}

DetailHtmlRenderResult DetailHtmlRenderer::renderPayload(const QJsonObject& payload) const
{
    if (!ready_) {
        LOG_WARN(LogCategory::WebViewKatex,
                 QStringLiteral("render payload skipped because renderer not ready error=%1").arg(lastError_));
        return makeFailure(lastError_.isEmpty() ? QStringLiteral("detail html renderer not ready") : lastError_);
    }

    const QByteArray payloadJson = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QString html = templateHtml_;
    html.replace(QStringLiteral("__DETAIL_PAYLOAD__"), QString::fromUtf8(payloadJson));
    if (html.contains(QStringLiteral("__DETAIL_PAYLOAD__"))) {
        const QString error = QStringLiteral("detail payload placeholder replacement failed template=%1").arg(templatePath_);
        LOG_ERROR(LogCategory::WebViewKatex, error);
        return makeFailure(error);
    }

    DetailHtmlRenderResult result;
    result.success = true;
    result.html = std::move(html);
    result.baseUrl = baseUrl_;
    return result;
}

QJsonObject DetailHtmlRenderer::toJson(const domain::adapters::ConclusionDetailViewData& detail)
{
    QJsonObject object;
    object.insert(QStringLiteral("title"), detail.title);
    object.insert(QStringLiteral("conclusionId"), detail.conclusionId);
    object.insert(QStringLiteral("module"), detail.module);
    object.insert(QStringLiteral("moduleKey"), detail.moduleKey);
    object.insert(QStringLiteral("summary"), detail.summary);
    object.insert(QStringLiteral("isValid"), detail.isValid);
    object.insert(QStringLiteral("errorMessage"), detail.errorMessage);

    QJsonArray tagsArray;
    for (const QString& tag : detail.tags) {
        tagsArray.append(tag);
    }
    object.insert(QStringLiteral("tags"), tagsArray);

    QJsonArray sectionsArray;
    for (const domain::adapters::DetailSectionViewData& section : detail.sections) {
        QJsonObject sectionObj;
        sectionObj.insert(QStringLiteral("key"), section.key);
        sectionObj.insert(QStringLiteral("title"), section.title);
        sectionObj.insert(QStringLiteral("html"), section.html);
        sectionObj.insert(QStringLiteral("text"), section.text);
        sectionObj.insert(QStringLiteral("kind"), normalizeSectionKind(section.kind));
        sectionObj.insert(QStringLiteral("visible"), section.visible);
        sectionsArray.append(sectionObj);
    }
    object.insert(QStringLiteral("sections"), sectionsArray);

    return object;
}

}  // namespace ui::detail

