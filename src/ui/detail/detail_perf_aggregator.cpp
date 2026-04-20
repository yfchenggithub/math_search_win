#include "ui/detail/detail_perf_aggregator.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"

#include <QMetaType>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <limits>

namespace ui::detail {
namespace {

QString normalizedDetailId(const QString& detailId)
{
    const QString normalized = detailId.trimmed();
    if (normalized.isEmpty() || normalized == QStringLiteral("-")) {
        return QStringLiteral("-");
    }
    return normalized;
}

qint64 toRoundedMs(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return -1;
    }

    bool ok = false;
    const qint64 intValue = value.toLongLong(&ok);
    if (ok) {
        return intValue;
    }

    const double doubleValue = value.toDouble(&ok);
    if (ok) {
        return qRound64(doubleValue);
    }

    QString text = value.toString().trimmed();
    if (text.isEmpty()) {
        return -1;
    }
    if (text.endsWith(QStringLiteral("ms"), Qt::CaseInsensitive)) {
        text.chop(2);
    }
    if (text.endsWith(QLatin1Char('%'))) {
        text.chop(1);
    }

    const qint64 parsedInt = text.toLongLong(&ok);
    if (ok) {
        return parsedInt;
    }
    const double parsedDouble = text.toDouble(&ok);
    if (ok) {
        return qRound64(parsedDouble);
    }
    return -1;
}

int toRoundedInt(const QVariant& value)
{
    const qint64 parsed = toRoundedMs(value);
    if (parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) {
        return -1;
    }
    return static_cast<int>(parsed);
}

qint64 durationFromExtras(const QVariantMap& extras, QStringView key)
{
    const QString normalizedKey = key.toString();
    if (!extras.contains(normalizedKey)) {
        return -1;
    }
    return toRoundedMs(extras.value(normalizedKey));
}

int intFromExtras(const QVariantMap& extras, QStringView key)
{
    const QString normalizedKey = key.toString();
    if (!extras.contains(normalizedKey)) {
        return -1;
    }
    return toRoundedInt(extras.value(normalizedKey));
}

QString stringFromExtras(const QVariantMap& extras, QStringView key)
{
    const QString normalizedKey = key.toString();
    if (!extras.contains(normalizedKey)) {
        return {};
    }
    return extras.value(normalizedKey).toString().trimmed();
}

int ratioPercent(qint64 partMs, qint64 totalMs)
{
    if (partMs <= 0 || totalMs <= 0) {
        return -1;
    }
    const double ratio = (static_cast<double>(partMs) / static_cast<double>(totalMs)) * 100.0;
    return std::clamp(static_cast<int>(qRound64(ratio)), 0, 100);
}

bool shouldPreferExplicitStageDt(QStringView displayPhase)
{
    return displayPhase == QStringLiteral("payload_ready") || displayPhase == QStringLiteral("web_dispatch_done")
           || displayPhase == QStringLiteral("light_done") || displayPhase == QStringLiteral("visible_katex_done")
           || displayPhase == QStringLiteral("heavy_done") || displayPhase == QStringLiteral("deferred_katex_done");
}

QString normalizeCancelReason(const QString& reason)
{
    const QString normalized = reason.trimmed();
    if (normalized.isEmpty()) {
        return QStringLiteral("canceled");
    }

    const QString lowered = normalized.toLower();
    if (lowered.contains(QStringLiteral("supersed")) || lowered.contains(QStringLiteral("stale"))
        || lowered.contains(QStringLiteral("newer_request"))) {
        return QStringLiteral("superseded");
    }
    return normalized;
}

QString valueToLogText(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return {};
    }

    if (value.metaType().id() == QMetaType::Bool) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }

    const qint64 rounded = toRoundedMs(value);
    if (rounded >= 0) {
        return QString::number(rounded);
    }
    return value.toString().trimmed();
}

void appendExtraIfPresent(QStringList* tokens, const QVariantMap& extras, QStringView key, QStringView alias = QStringView())
{
    if (tokens == nullptr) {
        return;
    }
    const QString normalizedKey = key.toString();
    if (!extras.contains(normalizedKey)) {
        return;
    }
    const QString value = valueToLogText(extras.value(normalizedKey));
    if (value.isEmpty()) {
        return;
    }
    const QString outputKey = alias.isEmpty() ? normalizedKey : alias.toString();
    tokens->append(QStringLiteral("%1=%2").arg(outputKey, value));
}

QString finishStatusForDisplayPhase(QStringView displayPhase)
{
    if (displayPhase == QStringLiteral("complete")) {
        return QStringLiteral("ok");
    }
    if (displayPhase == QStringLiteral("request_failed") || displayPhase == QStringLiteral("js_render_failed")) {
        return QStringLiteral("rejected");
    }
    return QStringLiteral("ok");
}

QString canonicalCacheState(const QString& cacheState)
{
    const QString normalized = cacheState.trimmed().toLower();
    if (normalized == QStringLiteral("hit") || normalized == QStringLiteral("miss") || normalized == QStringLiteral("unknown")) {
        return normalized;
    }
    return {};
}

}  // namespace

namespace detailperf {

QString toDisplayPhase(QStringView rawPhase)
{
    const QString normalized = rawPhase.toString().trimmed();
    if (normalized.isEmpty()) {
        return QStringLiteral("unknown");
    }

    if (normalized == QStringLiteral("selection_received")) {
        return QStringLiteral("selected");
    }
    if (normalized == QStringLiteral("detail_request_created")) {
        return QStringLiteral("request_created");
    }
    if (normalized == QStringLiteral("detail_payload_ready")) {
        return QStringLiteral("payload_ready");
    }
    if (normalized == QStringLiteral("dispatch_to_web_start")) {
        return QStringLiteral("web_dispatch_start");
    }
    if (normalized == QStringLiteral("dispatch_to_web_callback")) {
        return QStringLiteral("web_dispatch_done");
    }
    if (normalized == QStringLiteral("render_request_received")) {
        return QStringLiteral("render_received");
    }
    if (normalized == QStringLiteral("render_light_start")) {
        return QStringLiteral("light_start");
    }
    if (normalized == QStringLiteral("render_light_done")) {
        return QStringLiteral("light_done");
    }
    if (normalized == QStringLiteral("first_meaningful_paint_dispatch")) {
        return QStringLiteral("first_paint");
    }
    if (normalized == QStringLiteral("katex_visible_start")) {
        return QStringLiteral("visible_katex_start");
    }
    if (normalized == QStringLiteral("katex_visible_done")) {
        return QStringLiteral("visible_katex_done");
    }
    if (normalized == QStringLiteral("render_heavy_sections_start")) {
        return QStringLiteral("heavy_start");
    }
    if (normalized == QStringLiteral("render_heavy_sections_done")) {
        return QStringLiteral("heavy_done");
    }
    if (normalized == QStringLiteral("katex_deferred_start")) {
        return QStringLiteral("deferred_katex_start");
    }
    if (normalized == QStringLiteral("katex_deferred_done")) {
        return QStringLiteral("deferred_katex_done");
    }
    if (normalized == QStringLiteral("render_complete")) {
        return QStringLiteral("complete");
    }
    if (normalized == QStringLiteral("request_superseded")) {
        return QStringLiteral("superseded");
    }
    if (normalized == QStringLiteral("request_stale_ignored")) {
        return QStringLiteral("stale_ignored");
    }
    if (normalized == QStringLiteral("render_aborted_due_to_newer_request")) {
        return QStringLiteral("aborted_stale");
    }

    return normalized;
}

bool isKnownPhase(QStringView rawPhase)
{
    const QString normalized = rawPhase.toString().trimmed();
    if (normalized.isEmpty()) {
        return false;
    }

    static const QStringList kKnownPhases = {
        QStringLiteral("request_start"),
        QStringLiteral("selection_received"),
        QStringLiteral("detail_request_created"),
        QStringLiteral("detail_payload_ready"),
        QStringLiteral("data_ready"),
        QStringLiteral("dispatch_to_web_start"),
        QStringLiteral("dispatch_to_web_callback"),
        QStringLiteral("shell_not_ready_pending"),
        QStringLiteral("shell_ready_consume_pending"),
        QStringLiteral("web_load_finished"),
        QStringLiteral("render_request_received"),
        QStringLiteral("js_render_start"),
        QStringLiteral("render_light_start"),
        QStringLiteral("render_light_done"),
        QStringLiteral("first_meaningful_paint_dispatch"),
        QStringLiteral("katex_visible_start"),
        QStringLiteral("katex_visible_done"),
        QStringLiteral("render_heavy_sections_start"),
        QStringLiteral("render_heavy_sections_done"),
        QStringLiteral("katex_deferred_start"),
        QStringLiteral("katex_deferred_done"),
        QStringLiteral("deferred_collect_targets"),
        QStringLiteral("deferred_queue_built"),
        QStringLiteral("deferred_batch_start"),
        QStringLiteral("deferred_batch_done"),
        QStringLiteral("deferred_cancelled"),
        QStringLiteral("deferred_skipped_due_to_superseded"),
        QStringLiteral("heavy_section_dom_build"),
        QStringLiteral("heavy_section_dom_insert"),
        QStringLiteral("heavy_section_katex_render"),
        QStringLiteral("per_target_katex_start"),
        QStringLiteral("per_target_katex_done"),
        QStringLiteral("js_render_done"),
        QStringLiteral("render_complete"),
        QStringLiteral("request_failed"),
        QStringLiteral("js_render_failed"),
        QStringLiteral("request_superseded"),
        QStringLiteral("request_stale_ignored"),
        QStringLiteral("render_aborted_due_to_newer_request"),
    };

    return kKnownPhases.contains(normalized);
}

bool isKeyStage(QStringView displayPhase)
{
    return displayPhase == QStringLiteral("payload_ready") || displayPhase == QStringLiteral("first_paint")
           || displayPhase == QStringLiteral("visible_katex_done") || displayPhase == QStringLiteral("heavy_done")
           || displayPhase == QStringLiteral("deferred_katex_done") || displayPhase == QStringLiteral("complete");
}

bool isFinishPhase(QStringView displayPhase)
{
    return displayPhase == QStringLiteral("complete") || displayPhase == QStringLiteral("request_failed")
           || displayPhase == QStringLiteral("js_render_failed");
}

bool isCancelPhase(QStringView displayPhase)
{
    return displayPhase == QStringLiteral("superseded") || displayPhase == QStringLiteral("stale_ignored")
           || displayPhase == QStringLiteral("aborted_stale") || displayPhase == QStringLiteral("deferred_cancelled");
}

QVariantMap parsePerfExtra(const QString& extraRaw)
{
    QVariantMap parsed;
    const QString normalizedRaw = extraRaw.trimmed();
    if (normalizedRaw.isEmpty()) {
        return parsed;
    }

    parsed.insert(QStringLiteral("_raw"), normalizedRaw);

    static const QRegularExpression spacePattern(QStringLiteral("\\s+"));
    const QStringList tokens = normalizedRaw.split(spacePattern, Qt::SkipEmptyParts);
    for (const QString& token : tokens) {
        const int separatorPos = token.indexOf(QLatin1Char('='));
        if (separatorPos <= 0 || separatorPos >= token.size() - 1) {
            continue;
        }

        const QString key = token.left(separatorPos).trimmed();
        QString value = token.mid(separatorPos + 1).trimmed();
        if (key.isEmpty() || value.isEmpty()) {
            continue;
        }

        bool strippedMs = false;
        if (value.endsWith(QStringLiteral("ms"), Qt::CaseInsensitive)) {
            value.chop(2);
            strippedMs = true;
        }

        if (value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0) {
            parsed.insert(key, true);
            continue;
        }
        if (value.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0) {
            parsed.insert(key, false);
            continue;
        }

        bool intOk = false;
        const qlonglong intValue = value.toLongLong(&intOk);
        if (intOk) {
            parsed.insert(key, intValue);
            continue;
        }

        bool doubleOk = false;
        const double doubleValue = value.toDouble(&doubleOk);
        if (doubleOk) {
            parsed.insert(key, doubleValue);
            continue;
        }

        if (strippedMs) {
            parsed.insert(key, value.trimmed());
            continue;
        }

        parsed.insert(key, token.mid(separatorPos + 1).trimmed());
    }

    return parsed;
}

SlowLevel classifySlowStage(qint64 dtMs)
{
    if (dtMs < 100) {
        return SlowLevel::None;
    }
    if (dtMs < 500) {
        return SlowLevel::Warn;
    }
    if (dtMs < 2000) {
        return SlowLevel::Slow;
    }
    return SlowLevel::Critical;
}

}  // namespace detailperf

QString DetailPerfLogFormatter::formatBegin(const QString& detailId, quint64 requestId)
{
    return QStringLiteral("event=detail_begin request_id=%1 detail_id=%2")
        .arg(requestId)
        .arg(normalizedDetailId(detailId));
}

QString DetailPerfLogFormatter::formatStage(const DetailPerfStageEvent& event)
{
    QStringList tokens;
    tokens.append(QStringLiteral("event=detail_stage"));
    tokens.append(QStringLiteral("request_id=%1").arg(event.requestId));
    tokens.append(QStringLiteral("detail_id=%1").arg(normalizedDetailId(event.detailId)));
    tokens.append(QStringLiteral("phase=%1").arg(event.displayPhase));
    tokens.append(QStringLiteral("at_ms=%1").arg(event.atMs));
    if (event.dtMs >= 0) {
        tokens.append(QStringLiteral("stage_ms=%1").arg(event.dtMs));
    }

    if (event.displayPhase == QStringLiteral("payload_ready")) {
        appendExtraIfPresent(&tokens, event.extras, QStringLiteral("cache"));
        appendExtraIfPresent(&tokens, event.extras, QStringLiteral("sections"));
    } else if (event.displayPhase == QStringLiteral("visible_katex_done")) {
        appendExtraIfPresent(&tokens, event.extras, QStringLiteral("targets"));
        appendExtraIfPresent(&tokens, event.extras, QStringLiteral("katexMs"));
    } else if (event.displayPhase == QStringLiteral("heavy_done")) {
        appendExtraIfPresent(&tokens, event.extras, QStringLiteral("sections"));
    } else if (event.displayPhase == QStringLiteral("deferred_katex_done")) {
        appendExtraIfPresent(&tokens, event.extras, QStringLiteral("targets"));
        appendExtraIfPresent(&tokens, event.extras, QStringLiteral("katexMs"));
    }

    return tokens.join(QLatin1Char(' '));
}

QString DetailPerfLogFormatter::formatSlow(const DetailPerfStageEvent& event, int ratioPercent)
{
    QStringList tokens;
    tokens.append(QStringLiteral("event=detail_stage_slow"));
    tokens.append(QStringLiteral("request_id=%1").arg(event.requestId));
    tokens.append(QStringLiteral("detail_id=%1").arg(normalizedDetailId(event.detailId)));
    tokens.append(QStringLiteral("phase=%1").arg(event.displayPhase));
    tokens.append(QStringLiteral("stage_ms=%1").arg(event.dtMs));
    if (ratioPercent >= 0) {
        tokens.append(QStringLiteral("ratio_percent=%1").arg(ratioPercent));
    }
    return tokens.join(QLatin1Char(' '));
}

QString DetailPerfLogFormatter::formatCancel(quint64 oldRequestId,
                                             const QString& oldDetailId,
                                             quint64 newRequestId,
                                             const QString& newDetailId,
                                             const QString& reason)
{
    QStringList tokens;
    tokens.append(QStringLiteral("event=detail_cancel"));
    tokens.append(QStringLiteral("old_request_id=%1").arg(oldRequestId));
    tokens.append(QStringLiteral("old_detail_id=%1").arg(normalizedDetailId(oldDetailId)));
    if (newRequestId > 0) {
        tokens.append(QStringLiteral("new_request_id=%1").arg(newRequestId));
        tokens.append(QStringLiteral("new_detail_id=%1").arg(normalizedDetailId(newDetailId)));
    }
    tokens.append(QStringLiteral("reason=%1").arg(normalizeCancelReason(reason)));
    return tokens.join(QLatin1Char(' '));
}

QString DetailPerfLogFormatter::formatCancel(quint64 requestId, const QString& detailId, const QString& reason)
{
    QStringList tokens;
    tokens.append(QStringLiteral("event=detail_cancel"));
    tokens.append(QStringLiteral("request_id=%1").arg(requestId));
    tokens.append(QStringLiteral("detail_id=%1").arg(normalizedDetailId(detailId)));
    tokens.append(QStringLiteral("reason=%1").arg(normalizeCancelReason(reason)));
    return tokens.join(QLatin1Char(' '));
}

QString DetailPerfLogFormatter::formatEnd(const DetailPerfSummary& summary)
{
    QStringList tokens;
    tokens.append(QStringLiteral("event=detail_complete"));
    tokens.append(QStringLiteral("request_id=%1").arg(summary.requestId));
    tokens.append(QStringLiteral("detail_id=%1").arg(normalizedDetailId(summary.detailId)));
    tokens.append(QStringLiteral("status=%1").arg(summary.status.trimmed().isEmpty() ? QStringLiteral("ok") : summary.status.trimmed()));

    if (summary.totalMs >= 0) {
        tokens.append(QStringLiteral("elapsed_ms=%1").arg(summary.totalMs));
    }
    if (summary.fmpMs >= 0) {
        tokens.append(QStringLiteral("fmp_ms=%1").arg(summary.fmpMs));
    }
    if (summary.lightMs >= 0) {
        tokens.append(QStringLiteral("light_ms=%1").arg(summary.lightMs));
    }
    if (summary.visibleKatexMs >= 0) {
        tokens.append(QStringLiteral("visible_katex_ms=%1").arg(summary.visibleKatexMs));
    }
    if (summary.heavyMs >= 0) {
        tokens.append(QStringLiteral("heavy_ms=%1").arg(summary.heavyMs));
    }
    if (summary.deferredKatexMs >= 0) {
        tokens.append(QStringLiteral("deferred_katex_ms=%1").arg(summary.deferredKatexMs));
    }
    if (!summary.bottleneckPhase.trimmed().isEmpty()) {
        tokens.append(QStringLiteral("bottleneck=%1").arg(summary.bottleneckPhase));
    }
    if (summary.bottleneckDtMs >= 0) {
        tokens.append(QStringLiteral("bottleneck_ms=%1").arg(summary.bottleneckDtMs));
    }
    if (summary.bottleneckRatioPercent >= 0) {
        tokens.append(QStringLiteral("bottleneck_ratio_percent=%1").arg(summary.bottleneckRatioPercent));
    }
    if (!summary.cacheState.trimmed().isEmpty()) {
        tokens.append(QStringLiteral("cache=%1").arg(summary.cacheState));
    }
    if (summary.sectionCount >= 0) {
        tokens.append(QStringLiteral("sections=%1").arg(summary.sectionCount));
    }
    if (summary.visibleKatexTargets >= 0) {
        tokens.append(QStringLiteral("visibleTargets=%1").arg(summary.visibleKatexTargets));
    }
    if (summary.deferredKatexTargets >= 0) {
        tokens.append(QStringLiteral("deferredTargets=%1").arg(summary.deferredKatexTargets));
    }

    return tokens.join(QLatin1Char(' '));
}

void DetailPerfAggregator::beginRequest(const QString& detailId, quint64 requestId)
{
    if (requestId == 0) {
        return;
    }
    ensureState(detailId, requestId, true);
}

void DetailPerfAggregator::markSuperseded(quint64 oldRequestId,
                                          const QString& oldDetailId,
                                          quint64 newRequestId,
                                          const QString& newDetailId)
{
    if (oldRequestId == 0) {
        return;
    }

    QString resolvedOldDetailId = normalizedDetailId(oldDetailId);
    auto it = active_.find(oldRequestId);
    if (it != active_.end()) {
        if (resolvedOldDetailId == QStringLiteral("-")) {
            resolvedOldDetailId = normalizedDetailId(it->detailId);
        }
        it->canceled = true;
        it->superseded = true;
        it->finishReason = QStringLiteral("superseded");
        it->supersededByRequestId = newRequestId;
        it->supersededByDetailId = normalizedDetailId(newDetailId);
    }

    LOG_DEBUG(LogCategory::PerfDetail,
              DetailPerfLogFormatter::formatCancel(
                  oldRequestId, resolvedOldDetailId, newRequestId, normalizedDetailId(newDetailId), QStringLiteral("superseded")));

    if (it != active_.end()) {
        active_.erase(it);
    }
}

void DetailPerfAggregator::recordPhase(const QString& detailId,
                                       quint64 requestId,
                                       const QString& rawPhase,
                                       qint64 atMs,
                                       const QString& extraRaw)
{
    if (requestId == 0 || rawPhase.trimmed().isEmpty()) {
        return;
    }

    const QString displayPhase = detailperf::toDisplayPhase(rawPhase);
    if (detailperf::isFinishPhase(displayPhase)) {
        finishRequest(detailId, requestId, rawPhase, atMs, extraRaw);
        return;
    }
    if (detailperf::isCancelPhase(displayPhase)) {
        const QVariantMap extras = detailperf::parsePerfExtra(extraRaw);
        QString reason = stringFromExtras(extras, QStringLiteral("reason"));
        if (reason.isEmpty() && displayPhase == QStringLiteral("superseded")) {
            reason = QStringLiteral("superseded");
        }
        cancelRequest(detailId, requestId, reason);
        return;
    }

    DetailPerfRequestState* state = ensureState(detailId, requestId, true);
    if (state == nullptr || state->finished || state->canceled) {
        return;
    }

    const DetailPerfStageEvent event = appendEvent(state, detailId, rawPhase, atMs, extraRaw);
    if (event.isKeyStage) {
        emitStageLogs(*state, event);
    }
}

void DetailPerfAggregator::finishRequest(const QString& detailId,
                                         quint64 requestId,
                                         const QString& rawPhase,
                                         qint64 atMs,
                                         const QString& extraRaw)
{
    if (requestId == 0) {
        return;
    }

    DetailPerfRequestState* state = ensureState(detailId, requestId, true);
    if (state == nullptr || state->finished || state->canceled) {
        return;
    }

    const DetailPerfStageEvent event = appendEvent(state, detailId, rawPhase, atMs, extraRaw);
    if (event.isKeyStage) {
        emitStageLogs(*state, event);
    }

    const QString status = finishStatusForDisplayPhase(event.displayPhase);
    state->finished = true;
    state->finishReason = status;
    if (state->totalMs < 0 && atMs >= 0) {
        state->totalMs = atMs;
    }

    const DetailPerfSummary summary = buildSummary(state, status, atMs);
    LOG_DEBUG(LogCategory::PerfDetail, DetailPerfLogFormatter::formatEnd(summary));

    active_.remove(requestId);
}

void DetailPerfAggregator::cancelRequest(const QString& detailId, quint64 requestId, const QString& reason)
{
    if (requestId == 0) {
        return;
    }

    auto it = active_.find(requestId);
    if (it == active_.end()) {
        return;
    }

    it->canceled = true;
    it->finishReason = normalizeCancelReason(reason);
    if (it->finishReason == QStringLiteral("superseded")) {
        it->superseded = true;
    }

    const QString resolvedDetailId = detailId.trimmed().isEmpty() ? it->detailId : detailId.trimmed();
    LOG_DEBUG(LogCategory::PerfDetail,
              DetailPerfLogFormatter::formatCancel(requestId, resolvedDetailId, it->finishReason));
    active_.erase(it);
}

bool DetailPerfAggregator::hasActiveRequest(quint64 requestId) const
{
    if (requestId == 0) {
        return false;
    }
    return active_.contains(requestId);
}

void DetailPerfAggregator::clear()
{
    active_.clear();
}

DetailPerfRequestState* DetailPerfAggregator::ensureState(const QString& detailId, quint64 requestId, bool emitBeginIfNeeded)
{
    if (requestId == 0) {
        return nullptr;
    }

    auto it = active_.find(requestId);
    if (it == active_.end()) {
        DetailPerfRequestState initial;
        initial.requestId = requestId;
        initial.detailId = normalizedDetailId(detailId);
        initial.beginAtMs = 0;
        active_.insert(requestId, initial);
        it = active_.find(requestId);
    }
    if (it == active_.end()) {
        return nullptr;
    }

    const QString normalizedDetail = detailId.trimmed();
    if (!normalizedDetail.isEmpty() && normalizedDetail != QStringLiteral("-")) {
        it->detailId = normalizedDetail;
    }

    if (emitBeginIfNeeded && !it->began) {
        it->began = true;
        LOG_DEBUG(LogCategory::PerfDetail, DetailPerfLogFormatter::formatBegin(it->detailId, requestId));
    }

    return &it.value();
}

DetailPerfStageEvent DetailPerfAggregator::appendEvent(DetailPerfRequestState* state,
                                                       const QString& detailId,
                                                       const QString& rawPhase,
                                                       qint64 atMs,
                                                       const QString& extraRaw)
{
    DetailPerfStageEvent event;
    if (state == nullptr) {
        return event;
    }

    event.detailId = detailId.trimmed().isEmpty() ? state->detailId : detailId.trimmed();
    event.requestId = state->requestId;
    event.rawPhase = rawPhase.trimmed();
    event.displayPhase = detailperf::toDisplayPhase(rawPhase);
    event.atMs = std::max<qint64>(0, atMs);
    event.extraRaw = extraRaw.trimmed();
    event.extras = detailperf::parsePerfExtra(extraRaw);
    event.isKeyStage = detailperf::isKeyStage(event.displayPhase);

    const qint64 explicitDtMs = durationFromExtras(event.extras, QStringLiteral("dt"));
    if (event.isKeyStage) {
        const qint64 keyDeltaMs = state->keyStages.isEmpty()
                                      ? std::max<qint64>(0, event.atMs)
                                      : std::max<qint64>(0, event.atMs - state->keyStages.constLast().atMs);
        // Stage dt policy:
        // - prefer explicit dt only for semantically stable stage duration phases
        // - fallback to delta between key stages to keep the timeline readable and consistent
        if (shouldPreferExplicitStageDt(event.displayPhase) && explicitDtMs >= 0) {
            event.dtMs = explicitDtMs;
        } else if (keyDeltaMs >= 0) {
            event.dtMs = keyDeltaMs;
        } else {
            event.dtMs = explicitDtMs;
        }

        if (event.displayPhase != QStringLiteral("complete")) {
            const SlowLevel slowLevel = detailperf::classifySlowStage(event.dtMs);
            event.isSlow = (slowLevel != SlowLevel::None);
            event.isCriticalSlow = (slowLevel == SlowLevel::Critical);
        }
    } else {
        event.dtMs = explicitDtMs;
    }

    state->allEvents.push_back(event);
    if (event.isKeyStage) {
        state->keyStages.push_back(event);
    }
    updateSummaryFromEvent(state, event);
    return event;
}

void DetailPerfAggregator::updateSummaryFromEvent(DetailPerfRequestState* state, const DetailPerfStageEvent& event)
{
    if (state == nullptr) {
        return;
    }

    if (!event.detailId.trimmed().isEmpty() && event.detailId != QStringLiteral("-")) {
        state->detailId = event.detailId.trimmed();
    }

    const QString cache = canonicalCacheState(stringFromExtras(event.extras, QStringLiteral("cache")));
    if (!cache.isEmpty()) {
        state->cacheState = cache;
    }

    const int sections = intFromExtras(event.extras, QStringLiteral("sections"));
    if (sections >= 0) {
        state->sectionCount = sections;
    }

    if (event.displayPhase == QStringLiteral("visible_katex_done")) {
        const int targets = intFromExtras(event.extras, QStringLiteral("targets"));
        if (targets >= 0) {
            state->visibleKatexTargets = targets;
        }
    }
    if (event.displayPhase == QStringLiteral("deferred_katex_done")) {
        const int targets = intFromExtras(event.extras, QStringLiteral("targets"));
        if (targets >= 0) {
            state->deferredKatexTargets = targets;
        }
    }

    if (event.displayPhase == QStringLiteral("payload_ready")) {
        state->payloadMs = event.dtMs >= 0 ? event.dtMs : event.atMs;
    } else if (event.displayPhase == QStringLiteral("web_dispatch_done")) {
        state->webDispatchMs = event.dtMs;
    } else if (event.displayPhase == QStringLiteral("light_done")) {
        state->lightMs = event.dtMs;
    } else if (event.displayPhase == QStringLiteral("first_paint")) {
        state->fmpMs = event.atMs;
    } else if (event.displayPhase == QStringLiteral("visible_katex_done")) {
        state->visibleKatexMs = event.dtMs;
    } else if (event.displayPhase == QStringLiteral("heavy_done")) {
        state->heavyMs = event.dtMs;
    } else if (event.displayPhase == QStringLiteral("deferred_katex_done")) {
        state->deferredKatexMs = event.dtMs;
    } else if (event.displayPhase == QStringLiteral("complete")) {
        state->totalMs = event.atMs;
    }
}

void DetailPerfAggregator::emitStageLogs(const DetailPerfRequestState& state, const DetailPerfStageEvent& event) const
{
    LOG_DEBUG(LogCategory::PerfDetail, DetailPerfLogFormatter::formatStage(event));

    if (!event.isSlow || event.dtMs < 0) {
        return;
    }

    const SlowLevel level = detailperf::classifySlowStage(event.dtMs);
    int slowRatio = -1;
    if (level == SlowLevel::Slow || level == SlowLevel::Critical) {
        const qint64 denominator = state.totalMs > 0 ? state.totalMs : event.atMs;
        slowRatio = ratioPercent(event.dtMs, denominator);
    }
    LOG_DEBUG(LogCategory::PerfDetail, DetailPerfLogFormatter::formatSlow(event, slowRatio));
}

DetailPerfSummary DetailPerfAggregator::buildSummary(DetailPerfRequestState* state,
                                                     const QString& status,
                                                     qint64 fallbackTotalMs) const
{
    DetailPerfSummary summary;
    if (state == nullptr) {
        summary.status = status;
        summary.totalMs = fallbackTotalMs;
        return summary;
    }

    if (state->totalMs < 0 && fallbackTotalMs >= 0) {
        state->totalMs = fallbackTotalMs;
    }

    summary.detailId = state->detailId;
    summary.requestId = state->requestId;
    summary.status = status.trimmed().isEmpty() ? QStringLiteral("ok") : status.trimmed();
    summary.totalMs = state->totalMs;
    summary.fmpMs = state->fmpMs;
    summary.lightMs = state->lightMs;
    summary.visibleKatexMs = state->visibleKatexMs;
    summary.heavyMs = state->heavyMs;
    summary.deferredKatexMs = state->deferredKatexMs;
    summary.cacheState = state->cacheState;
    summary.sectionCount = state->sectionCount;
    summary.visibleKatexTargets = state->visibleKatexTargets;
    summary.deferredKatexTargets = state->deferredKatexTargets;

    for (const DetailPerfStageEvent& keyStage : state->keyStages) {
        if (keyStage.displayPhase == QStringLiteral("complete") || keyStage.dtMs < 0) {
            continue;
        }
        if (keyStage.dtMs > summary.bottleneckDtMs) {
            summary.bottleneckPhase = keyStage.displayPhase;
            summary.bottleneckDtMs = keyStage.dtMs;
        }
    }
    summary.bottleneckRatioPercent = ratioPercent(summary.bottleneckDtMs, summary.totalMs);

    state->bottleneckPhase = summary.bottleneckPhase;
    state->bottleneckDtMs = summary.bottleneckDtMs;
    state->bottleneckRatioPercent = summary.bottleneckRatioPercent;

    return summary;
}

}  // namespace ui::detail
