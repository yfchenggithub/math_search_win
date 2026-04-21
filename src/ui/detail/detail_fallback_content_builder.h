#pragma once

#include "domain/adapters/conclusion_detail_adapter.h"

#include <QString>

namespace ui::detail {

class DetailFallbackContentBuilder final {
public:
    static QString buildFallbackHtml(const domain::adapters::ConclusionDetailViewData& detailView);
    static QString buildTrialPreviewHtml(const domain::adapters::ConclusionDetailViewData& detailView,
                                         const QString& docId,
                                         const QString& previewDisabledReason,
                                         int snippetLimit = 220);
};

}  // namespace ui::detail
