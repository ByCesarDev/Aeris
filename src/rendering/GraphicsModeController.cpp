#include "rendering/GraphicsModeController.h"

#include <string>
#include <string_view>

#if __has_include("ll/api/mod/NativeMod.h")
#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/options/IOptionRegistry.h"
#include "mc/options/GraphicsMode.h"
#include "mc/options/option_types/BoolOption.h"
#include "mc/options/option_types/IntOption.h"
#include "mc/options/option_types/Option.h"
#include "mc/options/option_types/OptionID.h"
#include "mc/options/option_types/OptionType.h"
#endif

#include "core/ConfigManager.h"

namespace aeris {

namespace {

#if __has_include("ll/api/mod/NativeMod.h")

char const* modeName(ShaderRenderMode mode) {
    return mode == ShaderRenderMode::Deferred ? "Deferred" : "Forward";
}

char const* gameModeName(GraphicsMode mode) {
    switch (mode) {
    case GraphicsMode::Simple:
        return "Simple";
    case GraphicsMode::Fancy:
        return "Fancy";
    case GraphicsMode::Advanced:
        return "Advanced";
    case GraphicsMode::RayTraced:
        return "RayTraced";
    }
    return "Unknown";
}

Option* findOptionBySaveTag(IOptionRegistry& options, std::string_view expectedSaveTag, int& inOutHintId) {
    if (inOutHintId >= 0 && inOutHintId < 2000) {
        auto opt = options.getIfValid(static_cast<OptionID>(inOutHintId));
        if (opt.has_value() && *opt && (*opt)->mImpl) {
            if ((*opt)->mImpl.get()->mSaveTag.get() == expectedSaveTag) {
                return *opt;
            }
        }
    }

    // Dynamic scan across valid option IDs (0 to 1500)
    for (int id = 0; id < 1500; ++id) {
        auto opt = options.getIfValid(static_cast<OptionID>(id));
        if (opt.has_value() && *opt && (*opt)->mImpl) {
            if ((*opt)->mImpl.get()->mSaveTag.get() == expectedSaveTag) {
                inOutHintId = id;
                return *opt;
            }
        }
    }
    return nullptr;
}

bool setBoolOption(IClientInstance& client, std::string_view expectedSaveTag, bool value) {
    static int sCachedSwitchId = 798;
    Option* option = findOptionBySaveTag(client.getOptions(), expectedSaveTag, sCachedSwitchId);
    if (!option || !option->mImpl) return false;

    OptionType const type = option->mImpl.get()->mOptionType;
    if (type != OptionType::Boolean) return false;

    static_cast<BoolOption*>(option)->set(value, true);
    return true;
}

bool setGraphicsModeOption(IClientInstance& client, GraphicsMode mode) {
    static int sCachedModeId = 711;
    Option* option = findOptionBySaveTag(client.getOptions(), "graphics_mode", sCachedModeId);
    if (!option || !option->mImpl) return false;

    OptionType const type = option->mImpl.get()->mOptionType;
    if (type != OptionType::Int && type != OptionType::Enum) return false;

    static_cast<IntOption*>(option)->set(static_cast<int>(mode), true);
    return true;
}

bool setGraphicsModeOnClientThread(GraphicsMode mode, ShaderRenderMode shaderMode) {
    auto mod = ll::mod::NativeMod::current();

    // 1. Safe Mode Check: never mutate options if user enabled safeMode
    auto const& safety = ConfigManager::getInstance().config().safety;
    if (safety.safeMode) {
        if (mod) {
            mod->getLogger().info(
                "[GraphicsMode] Skipping graphics mode switch to {} (Safe Mode is ON)",
                gameModeName(mode)
            );
        }
        return false;
    }

    auto client = ll::service::getClientInstance();
    if (!client) {
        if (mod) {
            mod->getLogger().warn("[GraphicsMode] Cannot switch to {}: client instance unavailable", gameModeName(mode));
        }
        return false;
    }

    // 2. Enable in-game mode switch option flag
    bool const switchSet = setBoolOption(*client, "graphics_mode_switch", true);

    // 3. Set the target graphics mode
    bool const modeSet = setGraphicsModeOption(*client, mode);

    if (mod) {
        mod->getLogger().info(
            "[GraphicsMode] Shader mode {}: switchOptionSet={}, graphicsModeSet={}",
            modeName(shaderMode),
            switchSet,
            modeSet
        );
    }
    return modeSet;
}

#endif // __has_include("ll/api/mod/NativeMod.h")

} // namespace

bool GraphicsModeController::applyForRenderMode(ShaderRenderMode mode) {
#if __has_include("ll/api/mod/NativeMod.h")
    auto const target = (mode == ShaderRenderMode::Deferred) ? GraphicsMode::Advanced : GraphicsMode::Fancy;
    return setGraphicsModeOnClientThread(target, mode);
#else
    (void)mode;
    return false;
#endif
}

bool GraphicsModeController::applyForPack(ShaderPack const& pack) {
    return applyForRenderMode(pack.renderMode);
}

bool GraphicsModeController::applyForward() {
    return applyForRenderMode(ShaderRenderMode::Forward);
}

} // namespace aeris
