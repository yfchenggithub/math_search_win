#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace domain::models {

enum class RenderBlockKnownType {
    Unknown = 0,
    MathBlock,
    Paragraph,
    TheoremGroup,
};

enum class RenderTokenKnownType {
    Unknown = 0,
    Text,
    MathInline,
};

RenderBlockKnownType renderBlockKnownTypeFromString(const QString& rawType);
RenderTokenKnownType renderTokenKnownTypeFromString(const QString& rawType);

struct RenderToken {
    QString type;
    RenderTokenKnownType knownType = RenderTokenKnownType::Unknown;
    QString text;
    QString latex;
    QJsonObject extra;
};

struct TheoremGroupItem {
    QString title;
    QVector<RenderToken> descTokens;
    QString formulaLatex;
    QJsonObject extra;
};

struct RenderBlock {
    QString id;
    QString type;
    RenderBlockKnownType knownType = RenderBlockKnownType::Unknown;
    QString latex;
    QString align;
    QVector<RenderToken> tokens;
    QVector<TheoremGroupItem> items;
    QJsonObject extra;
};

struct RenderSection {
    QString key;
    QString title;
    QString blockType;
    QVector<RenderBlock> blocks;
    QJsonObject extra;
};

}  // namespace domain::models

