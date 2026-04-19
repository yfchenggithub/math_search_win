#pragma once

#include "domain/adapters/conclusion_detail_adapter.h"

#include <QJsonObject>
#include <QString>
#include <QUrl>

namespace ui::detail {

struct DetailHtmlRenderResult {
    bool success = false;
    QString html;
    QUrl baseUrl;
    QString errorMessage;
};

class DetailHtmlRenderer final {
public:
    DetailHtmlRenderer();

    bool isReady() const;
    QString lastError() const;
    QString detailDirectory() const;

    DetailHtmlRenderResult renderEmptyState(const QString& message = QString()) const;
    DetailHtmlRenderResult renderErrorState(const QString& message) const;
    DetailHtmlRenderResult renderContent(const domain::adapters::ConclusionDetailViewData& detail) const;

private:
    DetailHtmlRenderResult renderPayload(const QJsonObject& payload) const;
    static QJsonObject toJson(const domain::adapters::ConclusionDetailViewData& detail);

private:
    QString detailDirectory_;
    QString templatePath_;
    QString templateHtml_;
    QUrl baseUrl_;
    QString lastError_;
    bool ready_ = false;
};

}  // namespace ui::detail
