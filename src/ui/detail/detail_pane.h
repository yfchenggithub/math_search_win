#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QHash>
#include <QtGlobal>

class QTextBrowser;
class QWebEngineView;

namespace ui::detail {

class DetailHtmlRenderer;

class DetailPane final : public QObject {
    Q_OBJECT

public:
    struct RequestContext {
        QJsonObject payload;
        QString detailId;
        quint64 requestId = 0;
        qint64 selectionTimestampMs = 0;
    };

public:
    DetailPane(QWebEngineView* webView,
               QTextBrowser* fallbackBrowser,
               DetailHtmlRenderer* htmlRenderer,
               QObject* parent = nullptr);

    bool isWebModeEnabled() const;
    bool isShellReady() const;
    bool hasPendingRequest() const;

    void ensureShellLoaded();
    void setPendingRequest(const RequestContext& request);
    void renderDetail(const RequestContext& request);
    void resetViewportToTop();
    void renderStatePayload(const QJsonObject& payload);
    void consumePendingRequestIfReady();
    void disableWebMode();

signals:
    void shellReadyChanged(bool ready);
    void perfPhase(const QString& detailId,
                   quint64 requestId,
                   qint64 selectionTimestampMs,
                   qint64 atMs,
                   const QString& phase,
                   const QString& extra);
    void webModeFailed(const QString& reason);

private:
    void onShellLoadFinished(bool ok);
    void dispatchNow(const RequestContext& request);
    void emitPerf(const RequestContext& request, const QString& phase, const QString& extra = QString(), qint64 atMs = -1);
    void clearPendingRequest();
    void handleJsConsoleMessage(const QString& message);
    void rememberRequestContext(const RequestContext& request);
    qint64 selectionTimestampForRequest(quint64 requestId) const;
    QString detailIdForRequest(quint64 requestId, const QString& fallback) const;
    void pruneRequestContextCache();

private:
    QWebEngineView* webView_ = nullptr;
    QTextBrowser* fallbackBrowser_ = nullptr;
    DetailHtmlRenderer* htmlRenderer_ = nullptr;
    bool webModeEnabled_ = false;
    bool shellReady_ = false;
    bool shellLoadStarted_ = false;
    quint64 latestDispatchedRequestId_ = 0;
    bool hasPendingRequest_ = false;
    RequestContext pendingRequest_;
    QHash<quint64, RequestContext> requestContextById_;
};

}  // namespace ui::detail
