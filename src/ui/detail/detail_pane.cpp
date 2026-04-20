#include "ui/detail/detail_pane.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"
#include "ui/detail/detail_html_renderer.h"

#include <QDateTime>
#include <QTextBrowser>
#include <QVariantMap>
#include <QWebEnginePage>
#include <QWebEngineView>

#include <functional>

namespace ui::detail {
namespace {

class DetailWebPage final : public QWebEnginePage {
public:
    explicit DetailWebPage(std::function<void(const QString&)> consoleSink, QObject* parent = nullptr)
        : QWebEnginePage(parent), consoleSink_(std::move(consoleSink))
    {
    }

protected:
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level,
                                  const QString& message,
                                  int lineNumber,
                                  const QString& sourceId) override
    {
        if (consoleSink_) {
            QString decoratedMessage = message;
            if (!sourceId.trimmed().isEmpty()) {
                decoratedMessage.append(QStringLiteral(" source=%1:%2").arg(sourceId).arg(lineNumber));
            }
            consoleSink_(decoratedMessage);
        }
        QWebEnginePage::javaScriptConsoleMessage(level, message, lineNumber, sourceId);
    }

private:
    std::function<void(const QString&)> consoleSink_;
};

}  // namespace

DetailPane::DetailPane(QWebEngineView* webView,
                       QTextBrowser* fallbackBrowser,
                       DetailHtmlRenderer* htmlRenderer,
                       QObject* parent)
    : QObject(parent), webView_(webView), fallbackBrowser_(fallbackBrowser), htmlRenderer_(htmlRenderer)
{
    webModeEnabled_ = (webView_ != nullptr && htmlRenderer_ != nullptr && htmlRenderer_->isReady());

    if (!webModeEnabled_ || webView_ == nullptr) {
        return;
    }

    auto* page = new DetailWebPage([this](const QString& message) { handleJsConsoleMessage(message); }, webView_);
    webView_->setPage(page);
    connect(webView_, &QWebEngineView::loadFinished, this, &DetailPane::onShellLoadFinished);
}

bool DetailPane::isWebModeEnabled() const
{
    return webModeEnabled_;
}

bool DetailPane::isShellReady() const
{
    return shellReady_;
}

bool DetailPane::hasPendingRequest() const
{
    return hasPendingRequest_;
}

void DetailPane::ensureShellLoaded()
{
    if (!webModeEnabled_ || webView_ == nullptr || htmlRenderer_ == nullptr) {
        return;
    }
    if (shellReady_ || shellLoadStarted_) {
        return;
    }

    const QUrl templateUrl = htmlRenderer_->detailTemplateUrl();
    if (!templateUrl.isValid()) {
        emit webModeFailed(QStringLiteral("invalid detail template url"));
        return;
    }

    shellLoadStarted_ = true;
    shellReady_ = false;
    webView_->load(templateUrl);
}

void DetailPane::setPendingRequest(const RequestContext& request)
{
    pendingRequest_ = request;
    hasPendingRequest_ = true;
}

void DetailPane::renderDetail(const RequestContext& request)
{
    if (!webModeEnabled_ || webView_ == nullptr || htmlRenderer_ == nullptr) {
        return;
    }

    ensureShellLoaded();
    if (!shellReady_) {
        setPendingRequest(request);
        emitPerf(request, QStringLiteral("shell_not_ready_pending"));
        return;
    }

    dispatchNow(request);
}

void DetailPane::renderStatePayload(const QJsonObject& payload)
{
    RequestContext request;
    request.payload = payload;
    request.detailId = payload.value(QStringLiteral("detailId")).toString().trimmed();
    request.requestId = static_cast<quint64>(payload.value(QStringLiteral("requestId")).toVariant().toLongLong());
    request.selectionTimestampMs = 0;
    renderDetail(request);
}

void DetailPane::consumePendingRequestIfReady()
{
    if (!hasPendingRequest_ || !shellReady_) {
        return;
    }

    const RequestContext request = pendingRequest_;
    clearPendingRequest();
    emitPerf(request, QStringLiteral("shell_ready_consume_pending"));
    dispatchNow(request);
}

void DetailPane::disableWebMode()
{
    webModeEnabled_ = false;
    shellReady_ = false;
    shellLoadStarted_ = false;
    latestDispatchedRequestId_ = 0;
    clearPendingRequest();
}

void DetailPane::onShellLoadFinished(bool ok)
{
    shellLoadStarted_ = false;
    shellReady_ = ok;
    emit shellReadyChanged(ok);

    if (!ok) {
        emit webModeFailed(QStringLiteral("QWebEngineView failed to load detail html"));
        return;
    }

    if (webView_ != nullptr && webView_->page() != nullptr && htmlRenderer_ != nullptr) {
        webView_->page()->runJavaScript(htmlRenderer_->buildInitScript(), [](const QVariant&) {});
    }

    consumePendingRequestIfReady();
}

void DetailPane::dispatchNow(const RequestContext& request)
{
    if (webView_ == nullptr || webView_->page() == nullptr || htmlRenderer_ == nullptr) {
        return;
    }

    const QString script = htmlRenderer_->buildRenderScript(request.payload);
    const qint64 dispatchStartMs = QDateTime::currentMSecsSinceEpoch();

    if (request.requestId > 0) {
        latestDispatchedRequestId_ = request.requestId;
        emitPerf(request, QStringLiteral("dispatch_to_web_start"));
    }

    webView_->page()->runJavaScript(script, [this, request, dispatchStartMs](const QVariant& rawResult) {
        if (request.requestId > 0 && request.requestId != latestDispatchedRequestId_) {
            emitPerf(request,
                     QStringLiteral("request_superseded"),
                     QStringLiteral("reason=dispatch_callback_stale latest=%1").arg(latestDispatchedRequestId_));
            return;
        }

        const QVariantMap result = rawResult.toMap();
        const bool ok = result.value(QStringLiteral("ok"), true).toBool();
        const bool accepted = result.value(QStringLiteral("accepted"), true).toBool();
        const QString error = result.value(QStringLiteral("error")).toString().trimmed();
        const qint64 callbackMs = QDateTime::currentMSecsSinceEpoch() - dispatchStartMs;
        emitPerf(request,
                 QStringLiteral("dispatch_to_web_callback"),
                 QStringLiteral("dt=%1ms accepted=%2 ok=%3")
                     .arg(callbackMs)
                     .arg(accepted ? QStringLiteral("true") : QStringLiteral("false"))
                     .arg(ok ? QStringLiteral("true") : QStringLiteral("false")));

        if (!ok) {
            LOG_WARN(LogCategory::WebViewKatex,
                     QStringLiteral("detail runtime render error id=%1 req=%2 error=%3")
                         .arg(request.detailId)
                         .arg(request.requestId)
                         .arg(error.isEmpty() ? QStringLiteral("unknown") : error));
        }
    });

    if (webView_ != nullptr) {
        webView_->setVisible(true);
    }
    if (fallbackBrowser_ != nullptr) {
        fallbackBrowser_->setVisible(false);
    }
}

void DetailPane::emitPerf(const RequestContext& request, const QString& phase, const QString& extra)
{
    emit perfPhase(request.detailId, request.requestId, request.selectionTimestampMs, phase, extra);
}

void DetailPane::clearPendingRequest()
{
    hasPendingRequest_ = false;
    pendingRequest_ = RequestContext();
}

void DetailPane::handleJsConsoleMessage(const QString& message) const
{
    if (message.trimmed().startsWith(QStringLiteral("[perf][detail]"))) {
        LOG_DEBUG(LogCategory::DetailRender, message.trimmed());
        return;
    }
    LOG_DEBUG(LogCategory::WebViewKatex, message.trimmed());
}

}  // namespace ui::detail
