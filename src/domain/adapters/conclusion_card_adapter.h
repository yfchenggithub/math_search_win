#pragma once

#include "domain/models/conclusion_record.h"

#include <QString>
#include <QStringList>

namespace domain::adapters {

struct ConclusionCardViewData {
    QString id;
    QString title;
    QString module;
    QString category;
    int difficulty = 0;
    QStringList tags;
    QString summaryPlain;
    QString formulaFallbackText;
    QString formulaSvgPath;
    bool isPro = false;
    QString assetSvgName;
    QStringList searchKeywords;
    QStringList aliases;
};

class ConclusionCardAdapter final {
public:
    static ConclusionCardViewData toViewData(const models::ConclusionRecord& record);
};

}  // namespace domain::adapters

