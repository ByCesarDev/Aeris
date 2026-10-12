#include "ui/PauseMenuHooks.h"
#include "ui/ShaderManagerScreen.h"

#include <string>
#include <string_view>
#include <unordered_set>

#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"

#include "mc/client/gui/DirtyFlag.h"
#include "mc/client/gui/ViewRequest.h"
#include "mc/client/gui/controls/UIPropertyBag.h"
#include "mc/client/gui/screens/controllers/MinecraftScreenController.h"
#include "mc/client/gui/screens/controllers/PauseScreenController.h"

namespace aeris::ui {

namespace {

std::unordered_set<MinecraftScreenController*> gRegisteredControllers;

constexpr std::string_view kButtonOpenShaders = "button.aeris_open_shaders";

void ensurePauseButtonHandler(MinecraftScreenController& ctrl) {
    if (!gRegisteredControllers.insert(&ctrl).second) return;

    if (auto mod = ll::mod::NativeMod::current()) {
        mod->getLogger().info("[AerisUI] Registering button handler for '{}' on controller {:p}", kButtonOpenShaders, static_cast<void*>(&ctrl));
    }

    ctrl.registerButtonPressedHandler(ctrl._getNameId(std::string(kButtonOpenShaders)), [&ctrl](UIPropertyBag*) {
        if (auto mod = ll::mod::NativeMod::current()) {
            mod->getLogger().info(
                "[AerisUI] [1/4] Pause menu button '{}' pressed on controller {:p}! Opening ShaderManagerScreen on top of pause menu...",
                kButtonOpenShaders,
                static_cast<void*>(&ctrl)
            );
        }

        // Suspend pause menu input so it doesn't process clicks while the shaders screen is open
        ctrl.setSuspendInput(true);

        // Open ShaderManagerScreen directly on top of the pause screen (keeps world paused)
        ShaderManagerScreen::getInstance().open();

        // Consume the button click event without leaving or exiting the pause screen
        return ::ui::ViewRequest::ConsumeEvent;
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
    PauseScreenTickHook,
    ll::memory::HookPriority::Normal,
    PauseScreenController,
    &PauseScreenController::$tick,
    ::ui::DirtyFlag
) {
    // Keep pause menu input suspended while ShaderManagerScreen is open on top of it,
    // and automatically restore input the instant ShaderManagerScreen closes.
    setSuspendInput(ShaderManagerScreen::getInstance().visible());
    return origin();
}

LL_TYPE_INSTANCE_HOOK(
    PauseScreenTerminateHook,
    ll::memory::HookPriority::Normal,
    PauseScreenController,
    &PauseScreenController::$onTerminate,
    void
) {
    if (auto mod = ll::mod::NativeMod::current()) {
        mod->getLogger().info("[AerisUI] [4/4] PauseScreenController::$onTerminate called for controller {:p} - cleaning up", static_cast<void*>(this));
    }
    gRegisteredControllers.erase(this);
    origin();
}

void installPauseMenuHooks() {
    PauseScreenOpenHook::hook();
    PauseScreenTickHook::hook();
    PauseScreenTerminateHook::hook();
    if (auto mod = ll::mod::NativeMod::current()) {
        mod->getLogger().info("[AerisUI] Pause menu button hooks installed successfully (Open + Tick + Terminate)");
    }
}

void uninstallPauseMenuHooks() {
    PauseScreenTerminateHook::unhook();
    PauseScreenTickHook::unhook();
    PauseScreenOpenHook::unhook();
    gRegisteredControllers.clear();
}

} // namespace aeris::ui
