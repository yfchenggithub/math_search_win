#pragma once

namespace ui::detail {

enum class DetailRenderPath {
    TrialPreview = 0,
    Web,
    FallbackText,
};

class DetailRenderPathResolver final {
public:
    static DetailRenderPath resolve(bool fullDetailEnabled,
                                    bool webDetailEnabled,
                                    bool hasDetailPane,
                                    bool hasViewDataMapper);
};

}  // namespace ui::detail
