#pragma once

#include <QString>
#include <QtGlobal>

namespace ui::detail {

struct DetailRenderRequest {
    QString detailId;
    quint64 requestId = 0;
    qint64 selectionTimestampMs = 0;
};

struct DetailRenderRequestCreation {
    DetailRenderRequest request;
    quint64 supersededRequestId = 0;
    QString supersededDetailId;
};

class DetailRenderCoordinator final {
public:
    DetailRenderRequestCreation createRequest(const QString& detailId);
    bool isRequestStale(quint64 requestId) const;
    bool isSameAsRendered(const QString& detailId) const;
    void markRendered(const QString& detailId, quint64 requestId);
    void clearRenderedDetail();
    void reset();

private:
    quint64 nextRequestId_ = 0;
    quint64 latestRequestedRequestId_ = 0;
    QString latestRequestedDetailId_;
    QString lastRenderedDetailId_;
};

}  // namespace ui::detail
