#pragma once

#include <QHash>
#include <QString>
#include <QStringView>
#include <QVariantMap>
#include <QVector>
#include <QtGlobal>

namespace ui::detail {

enum class SlowLevel {
    None = 0,
    Warn,
    Slow,
    Critical,
};

struct DetailPerfStageEvent {
    QString detailId;
    quint64 requestId = 0;
    QString rawPhase;
    QString displayPhase;
    qint64 atMs = 0;
    qint64 dtMs = -1;
    QVariantMap extras;
    QString extraRaw;
    bool isKeyStage = false;
    bool isSlow = false;
    bool isCriticalSlow = false;
};

struct DetailPerfRequestState {
    QString detailId;
    quint64 requestId = 0;

    bool began = false;
    bool finished = false;
    bool canceled = false;
    bool superseded = false;
    QString finishReason;

    qint64 beginAtMs = 0;
    qint64 totalMs = -1;

    QVector<DetailPerfStageEvent> allEvents;
    QVector<DetailPerfStageEvent> keyStages;

    QString cacheState;
    int sectionCount = -1;
    int visibleKatexTargets = -1;
    int deferredKatexTargets = -1;

    qint64 payloadMs = -1;
    qint64 webDispatchMs = -1;
    qint64 lightMs = -1;
    qint64 fmpMs = -1;
    qint64 visibleKatexMs = -1;
    qint64 heavyMs = -1;
    qint64 deferredKatexMs = -1;

    QString bottleneckPhase;
    qint64 bottleneckDtMs = -1;
    int bottleneckRatioPercent = -1;

    quint64 supersededByRequestId = 0;
    QString supersededByDetailId;
};

struct DetailPerfSummary {
    QString detailId;
    quint64 requestId = 0;
    QString status;
    qint64 totalMs = -1;
    qint64 fmpMs = -1;
    qint64 lightMs = -1;
    qint64 visibleKatexMs = -1;
    qint64 heavyMs = -1;
    qint64 deferredKatexMs = -1;
    QString bottleneckPhase;
    qint64 bottleneckDtMs = -1;
    int bottleneckRatioPercent = -1;
    QString cacheState;
    int sectionCount = -1;
    int visibleKatexTargets = -1;
    int deferredKatexTargets = -1;
};

namespace detailperf {

QString toDisplayPhase(QStringView rawPhase);
bool isKnownPhase(QStringView rawPhase);
bool isKeyStage(QStringView displayPhase);
bool isFinishPhase(QStringView displayPhase);
bool isCancelPhase(QStringView displayPhase);
QVariantMap parsePerfExtra(const QString& extraRaw);
SlowLevel classifySlowStage(qint64 dtMs);

}  // namespace detailperf

class DetailPerfLogFormatter final {
public:
    static QString formatBegin(const QString& detailId, quint64 requestId);
    static QString formatStage(const DetailPerfStageEvent& event);
    static QString formatSlow(const DetailPerfStageEvent& event, int ratioPercent = -1);
    static QString formatCancel(quint64 oldRequestId,
                                const QString& oldDetailId,
                                quint64 newRequestId,
                                const QString& newDetailId,
                                const QString& reason);
    static QString formatCancel(quint64 requestId, const QString& detailId, const QString& reason);
    static QString formatEnd(const DetailPerfSummary& summary);
};

class DetailPerfAggregator final {
public:
    void beginRequest(const QString& detailId, quint64 requestId);

    void markSuperseded(quint64 oldRequestId,
                        const QString& oldDetailId,
                        quint64 newRequestId,
                        const QString& newDetailId);

    void recordPhase(const QString& detailId,
                     quint64 requestId,
                     const QString& rawPhase,
                     qint64 atMs,
                     const QString& extraRaw);

    void finishRequest(const QString& detailId,
                       quint64 requestId,
                       const QString& rawPhase,
                       qint64 atMs,
                       const QString& extraRaw);

    void cancelRequest(const QString& detailId, quint64 requestId, const QString& reason);

    bool hasActiveRequest(quint64 requestId) const;
    void clear();

private:
    DetailPerfRequestState* ensureState(const QString& detailId, quint64 requestId, bool emitBeginIfNeeded);
    DetailPerfStageEvent appendEvent(DetailPerfRequestState* state,
                                     const QString& detailId,
                                     const QString& rawPhase,
                                     qint64 atMs,
                                     const QString& extraRaw);
    void updateSummaryFromEvent(DetailPerfRequestState* state, const DetailPerfStageEvent& event);
    void emitStageLogs(const DetailPerfRequestState& state, const DetailPerfStageEvent& event) const;
    DetailPerfSummary buildSummary(DetailPerfRequestState* state, const QString& status, qint64 fallbackTotalMs) const;

    QHash<quint64, DetailPerfRequestState> active_;
};

}  // namespace ui::detail
