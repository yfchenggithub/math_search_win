#include "domain/models/search_result_models.h"

namespace domain::models {
namespace {

QString collapseWhitespace(const QString& input)
{
    QString output;
    output.reserve(input.size());

    bool lastWasSpace = false;
    for (QChar ch : input) {
        if (ch.isSpace()) {
            if (!lastWasSpace) {
                output.push_back(QChar::Space);
            }
            lastWasSpace = true;
            continue;
        }

        output.push_back(ch);
        lastWasSpace = false;
    }
    return output.trimmed();
}

QString lowercaseAsciiOnly(QString text)
{
    for (qsizetype i = 0; i < text.size(); ++i) {
        const ushort code = text.at(i).unicode();
        if (code >= 'A' && code <= 'Z') {
            text[i] = QChar(static_cast<ushort>(code + ('a' - 'A')));
        }
    }
    return text;
}

}  // namespace

QString normalizeQueryText(const QString& rawQuery)
{
    return lowercaseAsciiOnly(collapseWhitespace(rawQuery));
}

}  // namespace domain::models

