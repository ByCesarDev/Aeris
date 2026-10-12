#include "ui/PauseMenuHooks.h"
#include "ui/ShaderManagerScreen.h"

#include <string>
#include <string_view>
#include <unordered_set>

#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"

#include "mc/client/gui/ViewRequest.h"
#include "mc/client/gui/controls/UIPropertyBag.h"
#include "mc/client/gui/screens/controllers/MinecraftScreenController.h"
#include "mc/client/gui/screens/controllers/PauseScreenController.h"

namespace aeris::ui {

namespace {

std::unordered_set<MinecraftScreenController*> gRegisteredControllers;

constexpr std::string_view kButtonOpenShaders = "button.aeris_open_shaders";
constexpr auto kConsumeAndRefreshFocus = static_cast<::ui::ViewRequest>(
    static_cast<uint>(::ui::ViewRequest::ConsumeEvent) | static_cast<uint>(::ui::ViewRequest::DelayedFocusRefresh)
);

void ensurePauseButtonHandler(MinecraftScreenController& ctrl) {
    if (!gRegisteredControllers.insert(&ctrl).second) return;

    ctrl.registerButtonPressedHandler(ctrl._getNameId(std::string(kButtonOpenShaders)), [](UIPropertyBag*) {
        ShaderManagerScreen::getInstance().open();
        return kConsumeAndRefreshFocus;
    });
}

} // namespace

LL_TYPE_INSTANCE_HOOK(
    PauseScreenOpenHook,
    ll::memory::HookPriority::Normal,
    PauseScreenController,
    &PauseScreenController::$onOpen,
    void
) {
    origin();
    ensurePauseButtonHandler(*this);
}

void installPauseMenuHooks() {
    PauseScreenOpenHook::hook();
    if (auto mod = ll::mod::NativeMod::current()) {
        mod->getLogger().info("[AerisUI] Pause menu button hook installed successfully");
    }
}

void uninstallPauseMenuHooks() {
    PauseScreenOpenHook::unhook();
    gRegisteredControllers.clear();
}

} // namespace aeris::ui
