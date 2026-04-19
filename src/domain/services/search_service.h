#pragma once

#include "domain/models/search_result_models.h"

namespace infrastructure::data {
class ConclusionIndexRepository;
}

namespace domain::services {

class SearchService final {
public:
    explicit SearchService(const infrastructure::data::ConclusionIndexRepository* repository = nullptr);

    void setRepository(const infrastructure::data::ConclusionIndexRepository* repository);
    const infrastructure::data::ConclusionIndexRepository* repository() const;

    domain::models::SearchResult search(const QString& query,
                                        const domain::models::SearchOptions& options = {}) const;

private:
    const infrastructure::data::ConclusionIndexRepository* repository_ = nullptr;
};

}  // namespace domain::services

