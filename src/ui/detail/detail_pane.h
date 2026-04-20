#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
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
    void renderStatePayload(const QJsonObject& payload);
    void consumePendingRequestIfReady();
    void disableWebMode();

signals:
    void shellReadyChanged(bool ready);
    void perfPhase(const QString& detailId,
                   quint64 requestId,
                   qint64 selectionTimestampMs,
                   const QString& phase,
                   const QString& extra);
    void webModeFailed(const QString& reason);

private:
    void onShellLoadFinished(bool ok);
    void dispatchNow(const RequestContext& request);
    void emitPerf(const RequestContext& request, const QString& phase, const QString& extra = QString());
    void clearPendingRequest();
    void handleJsConsoleMessage(const QString& message) const;

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
};

}  // namespace ui::detail
