#pragma once

#include <QDateTime>
#include <QString>

namespace domain::models {

struct SearchHistoryItem {
    QString query;
    QString source = QStringLiteral("manual");
    QDateTime searchedAt;

    bool isValid() const
    {
        return !query.trimmed().isEmpty();
    }
};

}  // namespace domain::models

