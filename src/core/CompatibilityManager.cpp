#include "core/CompatibilityManager.h"

#include <algorithm>

#if __has_include("ll/api/Versions.h")
#include "ll/api/Versions.h"
#endif

namespace aeris {

CompatibilityManager::CompatibilityManager() {
    reset();
}

CompatibilityManager& CompatibilityManager::getInstance() {
    static CompatibilityManager instance;
    return instance;
}

void CompatibilityManager::reset() {
    mDetectedVersion         = "Unknown";
    mDetectedLoaderVersion   = "Unknown";
    mVersionCompatible       = false;
    mHotReloadSupported      = false;
    mGraphicsSwitchSupported = false;
}

bool CompatibilityManager::isVersionMatching(std::string_view version, std::string_view target) {
    if (version.empty() || version == "Unknown" || version == "?") {
        return false;
    }
    if (version == target) {
        return true;
    }
    // Tolerar variaciones comunes de nombrado de Bedrock para la misma versión (ej: 1.26.51.1 vs 1.26.51.01)
    if (version.starts_with("1.26.51") || version.starts_with("26.51")) {
        return true;
    }
    return false;
}

void CompatibilityManager::initialize() {
    reset();

#if __has_include("ll/api/Versions.h")
    try {
        mDetectedVersion = ll::getGameVersion().to_string();
    } catch (...) {
        mDetectedVersion = "Unknown";
    }

    try {
        mDetectedLoaderVersion = ll::getLoaderVersion().to_string();
    } catch (...) {
        mDetectedLoaderVersion = "Unknown";
    }
#endif

    initialize(mDetectedVersion, mDetectedLoaderVersion);
}

void CompatibilityManager::initialize(std::string gameVersion, std::string loaderVersion) {
    mDetectedVersion       = std::move(gameVersion);
    mDetectedLoaderVersion = std::move(loaderVersion);

    mVersionCompatible = isVersionMatching(mDetectedVersion, targetVersion());

    // Ninguna capacidad nativa de RenderDragon se asume activa hasta ser comprobada en su propio subsistema
    mHotReloadSupported      = false;
    mGraphicsSwitchSupported = false;
}

} // namespace aeris
