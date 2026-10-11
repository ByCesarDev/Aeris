#include "hooks/MaterialHooks.h"

#include <algorithm>
#include <atomic>
#include <iterator>
#include <string>
#include <utility>

#if __has_include("ll/api/memory/Hook.h")
#include "ll/api/memory/Hook.h"
#include "ll/api/memory/Signature.h"
#include "ll/api/mod/NativeMod.h"
#include "mc/deps/core/file/PathView.h"
#endif

#include "core/CompatibilityManager.h"
#include "core/ConfigManager.h"
#include "shaders/MaterialLoader.h"

namespace aeris {

namespace {

#if __has_include("ll/api/memory/Hook.h")

// Binary signatures inherited from previous research (1.26.20 / 1.26.30 / 1.26.40).
// Must be strictly verified at runtime before hooking on 1.26.51.01.
constexpr std::string_view readFilePatterns[] = {
    // 1.26.30 / 1.26.40
    "55 41 57 41 56 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 48 C7 85 ? ? ? ? ? ? ? ? 48 89 D6 41 0F 10 00",
    // 1.26.20
    "55 56 48 83 EC ? 48 8D 6C 24 ? 48 C7 45 ? ? ? ? ? 48 89 D6 48 8D 4D ? 4C 89 45 ? 4C 89 C2 E8 ? ? ? ? 48 8D 55 ? 48 89 F1 E8 ? ? ? ? 48 8B 4D ? E8 ? ? ? ? 48 89 F0 48 83 C4 ? 5E 5D C3 66 66 2E 0F 1F 84 00 ? ? ? ? 48 89 54 24 ? 55 56 48 83 EC ? 48 8D 6A ? 48 8B 4D ? E8 ? ? ? ? 90 48 83 C4 ? 5E 5D C3 55 56",
};

constexpr std::string_view endFramePatterns[] = {
    // 1.26.30 / 1.26.40
    "55 41 57 41 56 41 55 41 54 56 57 53 B8 ? ? ? ? E8 ? ? ? ? 48 29 C4 48 8D AC 24 ? ? ? ? 44 0F 29 8D ? ? ? ? 44 0F 29 85 ? ? ? ? 0F 29 BD ? ? ? ? 0F 29 B5 ? ? ? ? 48 C7 85 ? ? ? ? ? ? ? ? 48 89 95 ? ? ? ? 48 89 8D",
    // 1.26.20
    "55 41 57 41 56 41 55 41 54 56 57 53 B8 ? ? ? ? E8 ? ? ? ? 48 29 C4 48 8D AC 24 ? ? ? ? 66 44 0F 29 95",
};

constexpr std::string_view reloadMaterialPatterns[] = {
    // 1.26.20
    "C6 81 ? ? ? ? ? C3 CC CC CC CC CC CC CC CC 55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 0F 29 75",
};

void* resolveFirst(std::string_view const* patterns, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        if (void* address = ll::memory::Signature::parse(patterns[i]).view().resolve(true)) {
            return address;
        }
    }
    return nullptr;
}

using ReadFileFn       = std::string* (*)(void*, std::string*, Core::PathView*);
using EndFrameFn       = void (*)(uintptr_t, uintptr_t);
using ReloadMaterialFn = void (*)(uintptr_t);

ReadFileFn       g_readFileOriginal = nullptr;
EndFrameFn       g_endFrameOriginal = nullptr;
ReloadMaterialFn g_reloadMaterial   = nullptr;

void* g_readFileTarget = nullptr;
void* g_endFrameTarget = nullptr;

std::string* readFileDetour(void* self, std::string* result, Core::PathView* path) {
    if (g_readFileOriginal == nullptr) {
        return result;
    }

    std::string* ret = g_readFileOriginal(self, result, path);
    if (ret == nullptr || path == nullptr) {
        return ret;
    }

    // Bypass inmediato si no hay shaderpack activo o si el cargador está en modo vainilla
    if (!MaterialLoader::getInstance().isOverrideActive()) {
        return ret;
    }

    try {
        auto overrideBytes = MaterialLoader::getInstance().resolveMaterial(path->getUtf8StringView());
        if (overrideBytes.has_value() && !overrideBytes->empty()) {
            ret->assign(reinterpret_cast<char const*>(overrideBytes->data()), overrideBytes->size());

            static std::atomic<int> logged{0};
            if (logged.fetch_add(1) < 5) {
                if (auto mod = ll::mod::NativeMod::current()) {
                    mod->getLogger().info(
                        "[MaterialHooks] Overrode '{}' ({} bytes)",
                        path->getUtf8StringView(),
                        overrideBytes->size()
                    );
                }
            }
        }
    } catch (...) {
        // Fail open: jamás lanzar excepciones a través de los límites del hook del juego
    }

    return ret;
}

void endFrameDetour(uintptr_t self, uintptr_t context) {
    if (g_reloadMaterial != nullptr && MaterialHooks::getInstance().consumeReload()) {
        try {
            g_reloadMaterial(self);
            if (auto mod = ll::mod::NativeMod::current()) {
                mod->getLogger().info("[MaterialHooks] RenderDragon materials reloaded");
            }
        } catch (...) {
            // Protección del hilo de renderizado
        }
    }

    if (g_endFrameOriginal != nullptr) {
        g_endFrameOriginal(self, context);
    }
}

#endif // __has_include("ll/api/memory/Hook.h")

} // namespace

MaterialHooks& MaterialHooks::getInstance() {
    static MaterialHooks instance;
    return instance;
}

MaterialHooks::~MaterialHooks() {
    uninstall();
}

bool MaterialHooks::isInstalled() const {
    std::lock_guard lock(mMutex);
    return mInstalled;
}

bool MaterialHooks::isReloadAvailable() const {
    std::lock_guard lock(mMutex);
    return mReloadAvailable;
}

std::string MaterialHooks::lastError() const {
    std::lock_guard lock(mMutex);
    return mLastError;
}

void MaterialHooks::queueReload() {
    std::lock_guard lock(mMutex);
    mReloadPending = true;
}

bool MaterialHooks::consumeReload() {
    std::lock_guard lock(mMutex);
    if (!mReloadPending) {
        return false;
    }
    mReloadPending = false;
    return true;
}

void MaterialHooks::restoreVanilla() {
    std::lock_guard lock(mMutex);
    mReloadPending = false;
    MaterialLoader::getInstance().restoreVanilla();
}

bool MaterialHooks::install() {
    std::lock_guard lock(mMutex);
    if (mInstalled) {
        return true;
    }

#if __has_include("ll/api/memory/Hook.h")
    auto mod = ll::mod::NativeMod::current();
    if (!mod) {
        mLastError = "NativeMod context unavailable";
        return false;
    }
    auto& logger = mod->getLogger();

    // 1. MODO SEGURO: Ganchos experimentales desactivados por defecto
    auto const& safety = ConfigManager::getInstance().config().safety;
    if (safety.safeMode || !safety.enableExperimentalHooks) {
        mLastError = "RenderDragon native hooks disabled (safe mode active)";
        logger.info("MaterialHooks: {}. Memory untouched.", mLastError);
        CompatibilityManager::getInstance().setCapabilityState(
            FeatureCapability::MaterialRedirection,
            CapabilityState::Unknown,
            "cmd.info.reason.safemode"
        );
        CompatibilityManager::getInstance().setCapabilityState(
            FeatureCapability::MaterialHotReload,
            CapabilityState::Unknown,
            "cmd.info.reason.safemode"
        );
        CompatibilityManager::getInstance().setCapabilityState(
            FeatureCapability::GraphicsModeSwitch,
            CapabilityState::Unknown,
            "cmd.info.reason.safemode"
        );
        return false;
    }

    // 2. Verificar compatibilidad general de versión antes de tocar memoria
    if (!CompatibilityManager::getInstance().isVersionCompatible()) {
        mLastError = "Incompatible game version for RenderDragon hooks";
        logger.warn("MaterialHooks: {}; hooks safely aborted", mLastError);
        CompatibilityManager::getInstance().setCapabilityState(
            FeatureCapability::MaterialRedirection,
            CapabilityState::Unsupported,
            mLastError
        );
        return false;
    }

    // 3. Verificar reloadMaterial
    void* reloadTarget = resolveFirst(reloadMaterialPatterns, std::size(reloadMaterialPatterns));
    if (reloadTarget != nullptr) {
        g_reloadMaterial = reinterpret_cast<ReloadMaterialFn>(reloadTarget);
    } else {
        logger.info("MaterialHooks: reloadMaterial signature not found in 1.26.51.01; hot reload disabled");
    }

    // 4. Verificar y hookear readFile
    void* readFileTarget = resolveFirst(readFilePatterns, std::size(readFilePatterns));
    if (readFileTarget == nullptr) {
        mLastError = "readFile signature not verified for Minecraft 1.26.51.01";
        logger.warn("MaterialHooks: {}. Material redirection hook disabled to protect client stability.", mLastError);
        CompatibilityManager::getInstance().setCapabilityState(
            FeatureCapability::MaterialRedirection,
            CapabilityState::Unsupported,
            mLastError
        );
        return false;
    }

    ll::memory::hook(
        readFileTarget,
        ll::memory::toFuncPtr(&readFileDetour),
        reinterpret_cast<ll::memory::FuncPtr*>(&g_readFileOriginal),
        ll::memory::HookPriority::Normal
    );

    if (g_readFileOriginal == nullptr) {
        mLastError = "Hook installation on readFile target failed";
        logger.error("MaterialHooks: {}", mLastError);
        CompatibilityManager::getInstance().setCapabilityState(
            FeatureCapability::MaterialRedirection,
            CapabilityState::Unsupported,
            mLastError
        );
        return false;
    }

    g_readFileTarget = readFileTarget;
    logger.info("MaterialHooks: readFile detour hooked successfully at {:#x}", reinterpret_cast<uintptr_t>(readFileTarget));
    CompatibilityManager::getInstance().setCapabilityState(
        FeatureCapability::MaterialRedirection,
        CapabilityState::Supported,
        "readFile detour installed"
    );

    // 5. Verificar y hookear endFrame
    void* endFrameTarget = resolveFirst(endFramePatterns, std::size(endFramePatterns));
    if (endFrameTarget == nullptr) {
        logger.info("MaterialHooks: endFrame signature not found; frame-end reload trigger disabled");
        CompatibilityManager::getInstance().setCapabilityState(
            FeatureCapability::MaterialHotReload,
            CapabilityState::Unsupported,
            "endFrame signature not found in 1.26.51.01"
        );
    } else {
        ll::memory::hook(
            endFrameTarget,
            ll::memory::toFuncPtr(&endFrameDetour),
            reinterpret_cast<ll::memory::FuncPtr*>(&g_endFrameOriginal),
            ll::memory::HookPriority::Normal
        );

        if (g_endFrameOriginal == nullptr) {
            logger.warn("MaterialHooks: endFrame hook installation failed; reload trigger disabled");
            CompatibilityManager::getInstance().setCapabilityState(
                FeatureCapability::MaterialHotReload,
                CapabilityState::Unsupported,
                "endFrame hook installation failed"
            );
        } else {
            g_endFrameTarget = endFrameTarget;
            logger.info("MaterialHooks: endFrame detour hooked successfully");
            if (g_reloadMaterial != nullptr) {
                CompatibilityManager::getInstance().setCapabilityState(
                    FeatureCapability::MaterialHotReload,
                    CapabilityState::Supported,
                    "endFrame and reloadMaterial verified"
                );
            }
        }
    }

    mInstalled       = true;
    mReloadAvailable = (g_reloadMaterial != nullptr && g_endFrameOriginal != nullptr);
    return true;

#else
    mLastError = "LeviLamina Hook API headers unavailable";
    return false;
#endif
}

void MaterialHooks::uninstall() {
    std::lock_guard lock(mMutex);
    if (!mInstalled) {
        return;
    }

#if __has_include("ll/api/memory/Hook.h")
    if (g_readFileOriginal != nullptr && g_readFileTarget != nullptr) {
        ll::memory::unhook(g_readFileTarget, ll::memory::toFuncPtr(&readFileDetour));
        g_readFileOriginal = nullptr;
        g_readFileTarget   = nullptr;
    }

    if (g_endFrameOriginal != nullptr && g_endFrameTarget != nullptr) {
        ll::memory::unhook(g_endFrameTarget, ll::memory::toFuncPtr(&endFrameDetour));
        g_endFrameOriginal = nullptr;
        g_endFrameTarget   = nullptr;
    }

    g_reloadMaterial = nullptr;
#endif

    mInstalled       = false;
    mReloadAvailable = false;
    mReloadPending   = false;
}

} // namespace aeris
