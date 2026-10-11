#pragma once

#include <string>
#include <string_view>

namespace aeris {

enum class CapabilityState {
    Unknown,     // Not yet verified in current runtime environment
    Supported,   // Verified working in target environment
    Unsupported  // Incompatible, pattern not found or blocked for safety
};

enum class FeatureCapability {
    MaterialRedirection, // Hook to detour readFile for .material.bin redirection
    MaterialHotReload,   // Hook to endFrame + reloadMaterial for live shader updates
    GraphicsModeSwitch   // Runtime switching between Deferred and Forward pipelines
};

std::string_view capabilityStateToString(CapabilityState state);
std::string_view featureCapabilityToString(FeatureCapability cap);

class CompatibilityManager {
public:
    static CompatibilityManager& getInstance();

    // Inicializa detectando la versión real del juego y del loader
    void initialize();

    // Permite inicializar manualmente con versiones conocidas (útil para tests unitarios)
    void initialize(std::string gameVersion, std::string loaderVersion);

    [[nodiscard]] bool isVersionCompatible() const { return mVersionCompatible; }

    [[nodiscard]] bool isMaterialRedirectionSupported() const {
        return getCapabilityState(FeatureCapability::MaterialRedirection) == CapabilityState::Supported;
    }
    [[nodiscard]] bool isHotReloadSupported() const {
        return getCapabilityState(FeatureCapability::MaterialHotReload) == CapabilityState::Supported;
    }
    [[nodiscard]] bool isGraphicsSwitchSupported() const {
        return getCapabilityState(FeatureCapability::GraphicsModeSwitch) == CapabilityState::Supported;
    }

    [[nodiscard]] CapabilityState getCapabilityState(FeatureCapability cap) const;
    [[nodiscard]] std::string     getCapabilityReason(FeatureCapability cap) const;
    void                          setCapabilityState(FeatureCapability cap, CapabilityState state, std::string reason = "");

    [[nodiscard]] std::string const& detectedVersion() const { return mDetectedVersion; }
    [[nodiscard]] std::string const& rawDetectedVersion() const { return mRawDetectedVersion; }
    [[nodiscard]] std::string const& detectedLoaderVersion() const { return mDetectedLoaderVersion; }
    [[nodiscard]] std::string_view   targetVersion() const { return "1.26.51.01"; }

    // Helpers de control explícito (compatibilidad hacia atrás)
    void setHotReloadSupported(bool supported) {
        setCapabilityState(
            FeatureCapability::MaterialHotReload,
            supported ? CapabilityState::Supported : CapabilityState::Unsupported,
            supported ? "Manually enabled" : "Manually disabled"
        );
    }
    void setGraphicsSwitchSupported(bool supported) {
        setCapabilityState(
            FeatureCapability::GraphicsModeSwitch,
            supported ? CapabilityState::Supported : CapabilityState::Unsupported,
            supported ? "Manually enabled" : "Manually disabled"
        );
    }

    // Comprueba si una cadena de versión coincide con la versión objetivo (1.26.51.01, prefijo 1.26.51 o hash de build)
    static bool isVersionMatching(std::string_view version, std::string_view target = "1.26.51.01");

    void reset();

private:
    CompatibilityManager();

    struct CapabilityInfo {
        CapabilityState state  = CapabilityState::Unknown;
        std::string     reason = "Not evaluated";
    };

    std::string    mDetectedVersion       = "Unknown";
    std::string    mRawDetectedVersion    = "Unknown";
    std::string    mDetectedLoaderVersion = "Unknown";
    bool           mVersionCompatible     = false;
    CapabilityInfo mMaterialRedirection;
    CapabilityInfo mMaterialHotReload;
    CapabilityInfo mGraphicsModeSwitch;
};

} // namespace aeris
