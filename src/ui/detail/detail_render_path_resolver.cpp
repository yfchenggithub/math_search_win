#include "ui/detail/detail_render_path_resolver.h"

namespace ui::detail {

DetailRenderPath DetailRenderPathResolver::resolve(bool fullDetailEnabled,
                                                   bool webDetailEnabled,
                                                   bool hasDetailPane,
                                                   bool hasViewDataMapper)
{
    return resolveForMode(
        fullDetailEnabled, DetailRenderMode::Web, false, webDetailEnabled, hasDetailPane, hasViewDataMapper);
}

DetailRenderPath DetailRenderPathResolver::resolveForMode(bool fullDetailEnabled,
                                                           DetailRenderMode mode,
                                                           bool pdfDetailEnabled,
                                                           bool webDetailEnabled,
                                                           bool hasDetailPane,
                                                           bool hasViewDataMapper)
{
    if (!fullDetailEnabled) {
        return DetailRenderPath::TrialPreview;
    }

    if (mode == DetailRenderMode::Pdf) {
        return pdfDetailEnabled ? DetailRenderPath::Pdf : DetailRenderPath::FallbackText;
    }

    if (mode == DetailRenderMode::Web) {
        return (webDetailEnabled && hasDetailPane && hasViewDataMapper) ? DetailRenderPath::Web
                                                                         : DetailRenderPath::FallbackText;
    }

    if (pdfDetailEnabled) {
        return DetailRenderPath::Pdf;
    }
    if (webDetailEnabled && hasDetailPane && hasViewDataMapper) {
        return DetailRenderPath::Web;
    }
    return DetailRenderPath::FallbackText;
}

}  // namespace ui::detail
