#include "ui/detail/detail_html_renderer.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"
#include "shared/paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStringList>

namespace ui::detail {

DetailHtmlRenderer::DetailHtmlRenderer()
{
    const QString resourcesRoot = AppPaths::resourcesDir();
    detailDirectory_ = AppPaths::detailResourcesDir();
    detailTemplatePath_ = AppPaths::detailTemplatePath();

    const QString katexCssPath = QDir(AppPaths::katexDir()).filePath(QStringLiteral("katex.min.css"));
    const QString katexJsPath = QDir(AppPaths::katexDir()).filePath(QStringLiteral("katex.min.js"));
    const QString katexAutoRenderPath = QDir(AppPaths::katexDir()).filePath(QStringLiteral("contrib/auto-render.min.js"));
    const QString katexFontProbePath = QDir(AppPaths::katexDir()).filePath(QStringLiteral("fonts/KaTeX_Main-Regular.woff2"));
    const QString detailCssPath = QDir(detailDirectory_).filePath(QStringLiteral("detail.css"));
    const QString detailJsPath = QDir(detailDirectory_).filePath(QStringLiteral("detail.js"));

    LOG_DEBUG(LogCategory::WebViewKatex,
              QStringLiteral("renderer init app_root=%1 resources=%2 detail_dir=%3")
                  .arg(AppPaths::appRoot(), resourcesRoot, detailDirectory_));

    const QStringList requiredFiles = {
        detailTemplatePath_,
        detailCssPath,
        detailJsPath,
        katexCssPath,
        katexJsPath,
        katexAutoRenderPath,
        katexFontProbePath,
    };

    QStringList missingFiles;
    for (const QString& path : requiredFiles) {
        const QFileInfo info(path);
        if (!info.exists() || !info.isFile()) {
            missingFiles.push_back(path);
        } else {
            LOG_DEBUG(LogCategory::WebViewKatex, QStringLiteral("renderer resource_ready path=%1").arg(path));
        }
    }

    if (!missingFiles.isEmpty()) {
        lastError_ = QStringLiteral("missing required detail resources: %1").arg(missingFiles.join(QStringLiteral(" | ")));
        LOG_WARN(LogCategory::WebViewKatex, lastError_);
        return;
    }

    QFile templateFile(detailTemplatePath_);
    if (!templateFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        lastError_ = QStringLiteral("failed to open detail template path=%1 reason=%2")
                         .arg(detailTemplatePath_, templateFile.errorString());
        LOG_ERROR(LogCategory::WebViewKatex, lastError_);
        return;
    }

    const QString templateHtml = QString::fromUtf8(templateFile.readAll());
    templateFile.close();

    if (templateHtml.trimmed().isEmpty()) {
        lastError_ = QStringLiteral("detail template is empty path=%1").arg(detailTemplatePath_);
        LOG_ERROR(LogCategory::WebViewKatex, lastError_);
        return;
    }

    if (!templateHtml.contains(QStringLiteral("window.__DETAIL_INITIAL_DATA__"))) {
        lastError_ =
            QStringLiteral("detail template missing initial bootstrap marker path=%1").arg(detailTemplatePath_);
        LOG_ERROR(LogCategory::WebViewKatex, lastError_);
        return;
    }

    const QStringList expectedLocalRefs = {
        QStringLiteral("../katex/katex.min.css"),
        QStringLiteral("../katex/katex.min.js"),
        QStringLiteral("../katex/contrib/auto-render.min.js"),
        QStringLiteral("./detail.css"),
        QStringLiteral("./detail.js"),
    };
    for (const QString& expectedRef : expectedLocalRefs) {
        if (!templateHtml.contains(expectedRef)) {
            LOG_WARN(LogCategory::WebViewKatex,
                     QStringLiteral("detail template may miss local dependency ref=%1 path=%2")
                         .arg(expectedRef, detailTemplatePath_));
        }
    }

    detailTemplateUrl_ = QUrl::fromLocalFile(detailTemplatePath_);
    if (!detailTemplateUrl_.isValid()) {
        lastError_ = QStringLiteral("detail template url invalid path=%1").arg(detailTemplatePath_);
        LOG_ERROR(LogCategory::WebViewKatex, lastError_);
        return;
    }

    ready_ = true;
    lastError_.clear();

    LOG_INFO(LogCategory::WebViewKatex,
             QStringLiteral("renderer ready template=%1 template_url=%2")
                 .arg(detailTemplatePath_, detailTemplateUrl_.toString()));
    LOG_INFO(LogCategory::PerfWebView, QStringLiteral("event=renderer_ready mode=detail_html"));
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

QString DetailHtmlRenderer::detailTemplatePath() const
{
    return detailTemplatePath_;
}

QUrl DetailHtmlRenderer::detailTemplateUrl() const
{
    return detailTemplateUrl_;
}

QString DetailHtmlRenderer::buildInitScript() const
{
    return QStringLiteral(
        "(function(){"
        "if(window.DetailRuntime&&typeof window.DetailRuntime.initShell==='function'){"
        "return window.DetailRuntime.initShell();"
        "}"
        "return {ok:false,error:'detail_runtime_init_missing'};"
        "})();");
}

QString DetailHtmlRenderer::buildRenderScript(const QJsonObject& payload) const
{
    const QByteArray payloadJson = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    return QStringLiteral(
               "(function(){"
               "if(window.DetailRuntime&&typeof window.DetailRuntime.renderDetail==='function'){"
               "return window.DetailRuntime.renderDetail(%1);"
               "}"
               "return {ok:false,error:'detail_runtime_api_missing'};"
               "})();")
        .arg(QString::fromUtf8(payloadJson));
}

}  // namespace ui::detail
