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

    if (auto mod = ll::mod::NativeMod::current()) {
        mod->getLogger().info("[AerisUI] Registering button handler for '{}' on controller {:p}", kButtonOpenShaders, static_cast<void*>(&ctrl));
    }

    ctrl.registerButtonPressedHandler(ctrl._getNameId(std::string(kButtonOpenShaders)), [](UIPropertyBag*) {
        if (auto mod = ll::mod::NativeMod::current()) {
            mod->getLogger().info("[AerisUI] Pause menu button '{}' pressed! Opening ShaderManagerScreen...", kButtonOpenShaders);
        }
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
    if (auto mod = ll::mod::NativeMod::current()) {
        mod->getLogger().info("[AerisUI] PauseScreenController::$onOpen called for controller {:p}", static_cast<void*>(this));
    }
    origin();
    ensurePauseButtonHandler(*this);
}

LL_TYPE_INSTANCE_HOOK(
    PauseScreenTerminateHook,
    ll::memory::HookPriority::Normal,
    PauseScreenController,
    &PauseScreenController::$onTerminate,
    void
) {
    if (auto mod = ll::mod::NativeMod::current()) {
        mod->getLogger().info("[AerisUI] PauseScreenController::$onTerminate called for controller {:p} - cleaning up", static_cast<void*>(this));
    }
    gRegisteredControllers.erase(this);
    origin();
}

void installPauseMenuHooks() {
    PauseScreenOpenHook::hook();
    PauseScreenTerminateHook::hook();
    if (auto mod = ll::mod::NativeMod::current()) {
        mod->getLogger().info("[AerisUI] Pause menu button hooks installed successfully (Open + Terminate)");
    }
}

void uninstallPauseMenuHooks() {
    PauseScreenTerminateHook::unhook();
    PauseScreenOpenHook::unhook();
    gRegisteredControllers.clear();
}

} // namespace aeris::ui
