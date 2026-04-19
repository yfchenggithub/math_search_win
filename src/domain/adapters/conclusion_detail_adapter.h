#pragma once

#include "domain/models/conclusion_record.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace domain::adapters {

struct DetailSectionViewData {
    QString key;
    QString title;
    QString html;
    QString text;
    QString kind;
    bool visible = true;
};

struct ConclusionDetailViewData {
    QString title;
    QString conclusionId;
    QString module;
    QString moduleKey;
    QStringList tags;
    QString summary;
    QVector<DetailSectionViewData> sections;
    bool isValid = false;
    QString errorMessage;
};

class ConclusionDetailAdapter final {
public:
    static ConclusionDetailViewData toViewData(const models::ConclusionRecord& record);
};

}  // namespace domain::adapters
