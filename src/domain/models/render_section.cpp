#include "domain/models/render_section.h"

namespace domain::models {
namespace {

QString normalizeType(const QString& rawType)
{
    return rawType.trimmed().toLower();
}

}  // namespace

RenderBlockKnownType renderBlockKnownTypeFromString(const QString& rawType)
{
    const QString normalized = normalizeType(rawType);
    if (normalized == QStringLiteral("math_block")) {
        return RenderBlockKnownType::MathBlock;
    }
    if (normalized == QStringLiteral("paragraph")) {
        return RenderBlockKnownType::Paragraph;
    }
    if (normalized == QStringLiteral("theorem_group")) {
        return RenderBlockKnownType::TheoremGroup;
    }
    return RenderBlockKnownType::Unknown;
}

RenderTokenKnownType renderTokenKnownTypeFromString(const QString& rawType)
{
    const QString normalized = normalizeType(rawType);
    if (normalized == QStringLiteral("text")) {
        return RenderTokenKnownType::Text;
    }
    if (normalized == QStringLiteral("math_inline")) {
        return RenderTokenKnownType::MathInline;
    }
    return RenderTokenKnownType::Unknown;
}

}  // namespace domain::models

