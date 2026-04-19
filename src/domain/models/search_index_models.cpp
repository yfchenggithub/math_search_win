#include "domain/models/search_index_models.h"

#include <algorithm>

namespace domain::models {

QStringList decodeFieldMask(quint32 fieldMask, const FieldMaskLegend& legend)
{
    struct NameAndBit {
        QString name;
        quint32 bit = 0;
    };

    QVector<NameAndBit> decoded;
    decoded.reserve(legend.size());
    for (auto it = legend.constBegin(); it != legend.constEnd(); ++it) {
        const quint32 bit = it.value();
        if (bit == 0) {
            continue;
        }
        if ((fieldMask & bit) == bit) {
            decoded.push_back({it.key(), bit});
        }
    }

    std::sort(decoded.begin(), decoded.end(), [](const NameAndBit& lhs, const NameAndBit& rhs) {
        if (lhs.bit != rhs.bit) {
            return lhs.bit < rhs.bit;
        }
        const int compare = lhs.name.compare(rhs.name, Qt::CaseInsensitive);
        return compare == 0 ? lhs.name < rhs.name : compare < 0;
    });

    QStringList names;
    names.reserve(decoded.size());
    for (const NameAndBit& item : decoded) {
        names.push_back(item.name);
    }
    return names;
}

}  // namespace domain::models

