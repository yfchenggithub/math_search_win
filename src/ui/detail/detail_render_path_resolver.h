#pragma once

namespace ui::detail {

enum class DetailRenderMode {
    Auto = 0,
    Pdf,
    Web,
};

enum class DetailRenderPath {
    TrialPreview = 0,
    Pdf,
    Web,
    FallbackText,
};

class DetailRenderPathResolver final {
public:
    static DetailRenderPath resolve(bool fullDetailEnabled,
                                    bool webDetailEnabled,
                                    bool hasDetailPane,
                                    bool hasViewDataMapper);
    static DetailRenderPath resolveForMode(bool fullDetailEnabled,
                                           DetailRenderMode mode,
                                           bool pdfDetailEnabled,
                                           bool webDetailEnabled,
                                           bool hasDetailPane,
                                           bool hasViewDataMapper);
};

}  // namespace ui::detail
