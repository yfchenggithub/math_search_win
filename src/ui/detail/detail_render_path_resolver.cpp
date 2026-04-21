#include "ui/detail/detail_render_path_resolver.h"

namespace ui::detail {

DetailRenderPath DetailRenderPathResolver::resolve(bool fullDetailEnabled,
                                                   bool webDetailEnabled,
                                                   bool hasDetailPane,
                                                   bool hasViewDataMapper)
{
    if (!fullDetailEnabled) {
        return DetailRenderPath::TrialPreview;
    }

    if (webDetailEnabled && hasDetailPane && hasViewDataMapper) {
        return DetailRenderPath::Web;
    }

    return DetailRenderPath::FallbackText;
}

}  // namespace ui::detail
