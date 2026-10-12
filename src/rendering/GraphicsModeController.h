#pragma once

#include <string_view>
#include "shaders/ShaderPack.h"

namespace aeris {

/**
 * @brief Controls RenderDragon graphics mode switching between Forward (Fancy)
 * and Deferred (Advanced / Vibrant Visuals).
 * 
 * Verifies option IDs against save tags ("graphics_mode", "graphics_mode_switch"),
 * enforces safe mode checks, validates hardware compatibility, and executes
 * safely on the client thread.
 */
class GraphicsModeController {
public:
    static bool applyForRenderMode(ShaderRenderMode mode);
    static bool applyForPack(ShaderPack const& pack);
    static bool applyForward();
};

} // namespace aeris
