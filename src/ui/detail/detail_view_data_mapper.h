#pragma once

#include "domain/adapters/conclusion_detail_adapter.h"

#include <QJsonObject>
#include <QString>
#include <QtGlobal>

namespace ui::detail {

class DetailViewDataMapper final {
public:
    QJsonObject buildContentPayload(const domain::adapters::ConclusionDetailViewData& detail, quint64 requestId) const;
    QJsonObject buildEmptyPayload(const QString& message = QString()) const;
    QJsonObject buildErrorPayload(const QString& message,
                                  const QString& detailId = QString(),
                                  quint64 requestId = 0) const;
};

}  // namespace ui::detail
