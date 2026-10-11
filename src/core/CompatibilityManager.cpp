#include "core/CompatibilityManager.h"

#include <algorithm>

#if __has_include("ll/api/Versions.h")
#include "ll/api/Versions.h"
#endif

namespace aeris {

std::string_view capabilityStateToString(CapabilityState state) {
    switch (state) {
    case CapabilityState::Supported:
        return "Supported";
    case CapabilityState::Unsupported:
        return "Unsupported";
    case CapabilityState::Unknown:
    default:
        return "Unknown";
    }
}

std::string_view featureCapabilityToString(FeatureCapability cap) {
    switch (cap) {
    case FeatureCapability::MaterialRedirection:
        return "MaterialRedirection";
    case FeatureCapability::MaterialHotReload:
        return "MaterialHotReload";
    case FeatureCapability::GraphicsModeSwitch:
        return "GraphicsModeSwitch";
    default:
        return "UnknownCapability";
    }
}

CompatibilityManager::CompatibilityManager() {
    reset();
}

CompatibilityManager& CompatibilityManager::getInstance() {
    static CompatibilityManager instance;
    return instance;
}

void CompatibilityManager::reset() {
    mDetectedVersion         = "Unknown";
    mRawDetectedVersion      = "Unknown";
    mDetectedLoaderVersion   = "Unknown";
    mVersionCompatible       = false;

    mMaterialRedirection = {CapabilityState::Unknown, "Not evaluated"};
    mMaterialHotReload   = {CapabilityState::Unknown, "Not evaluated"};
    mGraphicsModeSwitch  = {CapabilityState::Unknown, "Not evaluated"};
}

CapabilityState CompatibilityManager::getCapabilityState(FeatureCapability cap) const {
    switch (cap) {
    case FeatureCapability::MaterialRedirection:
        return mMaterialRedirection.state;
    case FeatureCapability::MaterialHotReload:
        return mMaterialHotReload.state;
    case FeatureCapability::GraphicsModeSwitch:
        return mGraphicsModeSwitch.state;
    default:
        return CapabilityState::Unknown;
    }
}

std::string CompatibilityManager::getCapabilityReason(FeatureCapability cap) const {
    switch (cap) {
    case FeatureCapability::MaterialRedirection:
        return mMaterialRedirection.reason;
    case FeatureCapability::MaterialHotReload:
        return mMaterialHotReload.reason;
    case FeatureCapability::GraphicsModeSwitch:
        return mGraphicsModeSwitch.reason;
    default:
        return "Unknown capability";
    }
}

void CompatibilityManager::setCapabilityState(FeatureCapability cap, CapabilityState state, std::string reason) {
    CapabilityInfo* target = nullptr;
    switch (cap) {
    case FeatureCapability::MaterialRedirection:
        target = &mMaterialRedirection;
        break;
    case FeatureCapability::MaterialHotReload:
        target = &mMaterialHotReload;
        break;
    case FeatureCapability::GraphicsModeSwitch:
        target = &mGraphicsModeSwitch;
        break;
    }

    if (target) {
        target->state = state;
        if (!reason.empty()) {
            target->reason = std::move(reason);
        } else {
            target->reason = std::string(capabilityStateToString(state));
        }
    }
}

bool CompatibilityManager::isVersionMatching(std::string_view version, std::string_view target) {
    if (version.empty() || version == "Unknown" || version == "?") {
        return false;
    }
    if (version == target) {
        return true;
    }
    // Tolerar variaciones comunes de nombrado de Bedrock para la misma versión (ej: 1.26.51.1 vs 1.26.51.01 o build hash 1.26.51+...)
    if (version.starts_with("1.26.51") || version.starts_with("26.51")) {
        return true;
    }
    return false;
}

void CompatibilityManager::initialize() {
    reset();

#if __has_include("ll/api/Versions.h")
    try {
        mRawDetectedVersion = ll::getGameVersion().to_string();
    } catch (...) {
        mRawDetectedVersion = "Unknown";
    }

    try {
        mDetectedLoaderVersion = ll::getLoaderVersion().to_string();
    } catch (...) {
        mDetectedLoaderVersion = "Unknown";
    }
#endif

    initialize(mRawDetectedVersion, mDetectedLoaderVersion);
}

void CompatibilityManager::initialize(std::string gameVersion, std::string loaderVersion) {
    mRawDetectedVersion    = std::move(gameVersion);
    mDetectedLoaderVersion = std::move(loaderVersion);

    // Normalizar a la versión canónica de distribución si es la serie 1.26.51
    if (mRawDetectedVersion.starts_with("1.26.51")) {
        mDetectedVersion = "1.26.51.01";
    } else {
        mDetectedVersion = mRawDetectedVersion;
    }

    mVersionCompatible = isVersionMatching(mRawDetectedVersion, targetVersion());

    if (!mVersionCompatible) {
        std::string reason = "Target version mismatch (detected: " + mDetectedVersion + ", required: " + std::string(targetVersion()) + ")";
        setCapabilityState(FeatureCapability::MaterialRedirection, CapabilityState::Unsupported, reason);
        setCapabilityState(FeatureCapability::MaterialHotReload, CapabilityState::Unsupported, reason);
        setCapabilityState(FeatureCapability::GraphicsModeSwitch, CapabilityState::Unsupported, reason);
    } else {
        // En versión compatible, las capacidades empiezan en Unknown hasta ser verificadas en runtime por sus subsistemas
        setCapabilityState(FeatureCapability::MaterialRedirection, CapabilityState::Unknown, "cmd.info.reason.pending_verification");
        setCapabilityState(FeatureCapability::MaterialHotReload, CapabilityState::Unknown, "cmd.info.reason.pending_verification");
        setCapabilityState(FeatureCapability::GraphicsModeSwitch, CapabilityState::Unknown, "cmd.info.reason.pending_verification");
    }
}

} // namespace aeris
