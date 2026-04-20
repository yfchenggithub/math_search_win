#pragma once

#include <QJsonObject>
#include <QString>
#include <QUrl>

namespace ui::detail {

class DetailHtmlRenderer final {
public:
    DetailHtmlRenderer();

    bool isReady() const;
    QString lastError() const;
    QString detailDirectory() const;
    QString detailTemplatePath() const;
    QUrl detailTemplateUrl() const;

    QString buildInitScript() const;
    QString buildRenderScript(const QJsonObject& payload) const;

private:
    QString detailDirectory_;
    QString detailTemplatePath_;
    QUrl detailTemplateUrl_;
    QString lastError_;
    bool ready_ = false;
};

}  // namespace ui::detail
