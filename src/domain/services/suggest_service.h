#pragma once

#include "domain/models/search_result_models.h"

namespace infrastructure::data {
class ConclusionIndexRepository;
}

namespace domain::services {

class SuggestService final {
public:
    explicit SuggestService(const infrastructure::data::ConclusionIndexRepository* repository = nullptr);

    void setRepository(const infrastructure::data::ConclusionIndexRepository* repository);
    const infrastructure::data::ConclusionIndexRepository* repository() const;

    domain::models::SuggestionResult suggest(const QString& query,
                                             const domain::models::SuggestOptions& options = {}) const;

private:
    const infrastructure::data::ConclusionIndexRepository* repository_ = nullptr;
};

}  // namespace domain::services

