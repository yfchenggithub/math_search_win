#include "domain/models/conclusion_record.h"

namespace domain::models {

bool PlainContent::isEmpty() const
{
    return statement.isEmpty() && explanation.isEmpty() && proof.isEmpty() && examples.isEmpty() && traps.isEmpty()
           && summary.isEmpty();
}

bool ConclusionContent::hasSections() const
{
    return !sections.isEmpty();
}

bool ConclusionRecord::hasStructuredSections() const
{
    return content.hasSections();
}

}  // namespace domain::models

