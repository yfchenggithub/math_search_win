#include "ui/detail/detail_pane.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"
#include "ui/detail/detail_html_renderer.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QTextBrowser>
#include <QVariantMap>
#include <QWebEnginePage>
#include <QWebEngineView>

#include <algorithm>
#include <functional>

namespace ui::detail {
namespace {

constexpr int kMaxRememberedRequestContextCount = 96;

struct JsPerfRecord {
    QString detailId;
    quint64 requestId = 0;
    qint64 atMs = -1;
    QString phase;
    QString extra;
};

bool tryParseJsPerfRecord(const QString& message, JsPerfRecord* parsed)
{
    if (parsed == nullptr) {
        return false;
    }

    static const QRegularExpression pattern(
        QStringLiteral(
            R"(^\[perf\]\[detail\]\s+id=([^\s]+)\s+req=(\d+)\s+phase=([^\s]+)\s+t=([0-9]+(?:\.[0-9]+)?)ms(?:\s+(.*))?$)"));

    const QRegularExpressionMatch match = pattern.match(message.trimmed());
    if (!match.hasMatch()) {
        return false;
    }

    bool ok = false;
    const quint64 requestId = match.captured(2).toULongLong(&ok);
    if (!ok || requestId == 0) {
        return false;
    }

    bool elapsedOk = false;
    const double atMs = match.captured(4).toDouble(&elapsedOk);
    if (!elapsedOk) {
        return false;
    }

    parsed->detailId = match.captured(1).trimmed();
    parsed->requestId = requestId;
    parsed->phase = match.captured(3).trimmed();
    parsed->atMs = std::max<qint64>(0, qRound64(atMs));
    parsed->extra = match.captured(5).trimmed();
    return !parsed->phase.isEmpty();
}

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
    rememberRequestContext(request);
}

void DetailPane::renderDetail(const RequestContext& request)
{
    if (!webModeEnabled_ || webView_ == nullptr || htmlRenderer_ == nullptr) {
        return;
    }

    rememberRequestContext(request);
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
    requestContextById_.clear();
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

    if (hasPendingRequest_) {
        emitPerf(pendingRequest_, QStringLiteral("web_load_finished"));
    }

    consumePendingRequestIfReady();
}

void DetailPane::dispatchNow(const RequestContext& request)
{
    if (webView_ == nullptr || webView_->page() == nullptr || htmlRenderer_ == nullptr) {
        return;
    }

    rememberRequestContext(request);
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
            emitPerf(request,
                     QStringLiteral("request_stale_ignored"),
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

        if (!accepted) {
            emitPerf(request, QStringLiteral("request_stale_ignored"), QStringLiteral("reason=runtime_not_accepted"));
            return;
        }

        if (!ok) {
            emitPerf(request,
                     QStringLiteral("request_failed"),
                     QStringLiteral("reason=%1").arg(error.isEmpty() ? QStringLiteral("runtime_unknown") : error));
            LOG_WARN_F(LogCategory::WebViewKatex,
                       "DetailPane::onRenderCallback",
                       QStringLiteral("render callback_error detail_id=%1 request_id=%2 error=%3")
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

void DetailPane::emitPerf(const RequestContext& request, const QString& phase, const QString& extra, qint64 atMs)
{
    emit perfPhase(request.detailId, request.requestId, request.selectionTimestampMs, atMs, phase, extra);
}

void DetailPane::clearPendingRequest()
{
    hasPendingRequest_ = false;
    pendingRequest_ = RequestContext();
}

void DetailPane::handleJsConsoleMessage(const QString& message)
{
    const QString trimmedMessage = message.trimmed();
    if (trimmedMessage.startsWith(QStringLiteral("[perf][detail]"))) {
        JsPerfRecord parsed;
        if (tryParseJsPerfRecord(trimmedMessage, &parsed)) {
            const qint64 selectionTimestampMs = selectionTimestampForRequest(parsed.requestId);
            const QString detailId = detailIdForRequest(parsed.requestId, parsed.detailId);
            emit perfPhase(detailId, parsed.requestId, selectionTimestampMs, parsed.atMs, parsed.phase, parsed.extra);
        } else {
            LOG_DEBUG(LogCategory::DetailRender, trimmedMessage);
        }
        return;
    }
    LOG_DEBUG(LogCategory::WebViewKatex, trimmedMessage);
}

void DetailPane::rememberRequestContext(const RequestContext& request)
{
    if (request.requestId == 0) {
        return;
    }
    requestContextById_.insert(request.requestId, request);
    pruneRequestContextCache();
}

qint64 DetailPane::selectionTimestampForRequest(quint64 requestId) const
{
    if (requestId == 0) {
        return 0;
    }
    const auto it = requestContextById_.constFind(requestId);
    if (it == requestContextById_.constEnd()) {
        return 0;
    }
    return it.value().selectionTimestampMs;
}

QString DetailPane::detailIdForRequest(quint64 requestId, const QString& fallback) const
{
    if (requestId > 0) {
        const auto it = requestContextById_.constFind(requestId);
        if (it != requestContextById_.constEnd() && !it.value().detailId.trimmed().isEmpty()) {
            return it.value().detailId.trimmed();
        }
    }

    const QString normalizedFallback = fallback.trimmed();
    if (normalizedFallback == QStringLiteral("-")) {
        return {};
    }
    return normalizedFallback;
}

void DetailPane::pruneRequestContextCache()
{
    if (requestContextById_.size() <= kMaxRememberedRequestContextCount) {
        return;
    }

    if (latestDispatchedRequestId_ == 0) {
        requestContextById_.clear();
        return;
    }

    for (auto it = requestContextById_.begin(); it != requestContextById_.end();) {
        const quint64 requestId = it.key();
        if (requestId + static_cast<quint64>(kMaxRememberedRequestContextCount) < latestDispatchedRequestId_) {
            it = requestContextById_.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace ui::detail
