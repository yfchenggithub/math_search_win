#include "ui/detail/detail_render_coordinator.h"

#include <QDateTime>

namespace ui::detail {

DetailRenderRequestCreation DetailRenderCoordinator::createRequest(const QString& detailId)
{
    DetailRenderRequestCreation creation;
    const QString normalizedDetailId = detailId.trimmed();
    if (normalizedDetailId.isEmpty()) {
        return creation;
    }

    creation.supersededRequestId = latestRequestedRequestId_;
    creation.supersededDetailId = latestRequestedDetailId_;

    nextRequestId_ += 1;
    creation.request.detailId = normalizedDetailId;
    creation.request.requestId = nextRequestId_;
    creation.request.selectionTimestampMs = QDateTime::currentMSecsSinceEpoch();

    latestRequestedRequestId_ = creation.request.requestId;
    latestRequestedDetailId_ = creation.request.detailId;

    if (creation.supersededRequestId == 0 || creation.supersededRequestId == creation.request.requestId) {
        creation.supersededRequestId = 0;
        creation.supersededDetailId.clear();
    }

    return creation;
}

bool DetailRenderCoordinator::isRequestStale(quint64 requestId) const
{
    if (requestId == 0 || latestRequestedRequestId_ == 0) {
        return false;
    }
    return requestId < latestRequestedRequestId_;
}

bool DetailRenderCoordinator::isSameAsRendered(const QString& detailId) const
{
    return !detailId.trimmed().isEmpty() && detailId.trimmed() == lastRenderedDetailId_;
}

void DetailRenderCoordinator::markRendered(const QString& detailId, quint64 requestId)
{
    const QString normalizedDetailId = detailId.trimmed();
    if (normalizedDetailId.isEmpty() || requestId == 0) {
        return;
    }
    if (requestId != latestRequestedRequestId_) {
        return;
    }
    lastRenderedDetailId_ = normalizedDetailId;
}

void DetailRenderCoordinator::clearRenderedDetail()
{
    lastRenderedDetailId_.clear();
}

void DetailRenderCoordinator::reset()
{
    nextRequestId_ = 0;
    latestRequestedRequestId_ = 0;
    latestRequestedDetailId_.clear();
    lastRenderedDetailId_.clear();
}

}  // namespace ui::detail
