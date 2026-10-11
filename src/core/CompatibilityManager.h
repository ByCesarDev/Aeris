#pragma once

#include <string>
#include <string_view>

namespace aeris {

class CompatibilityManager {
public:
    static CompatibilityManager& getInstance();

    // Inicializa detectando la versión real del juego y del loader
    void initialize();

    // Permite inicializar manualmente con versiones conocidas (útil para tests unitarios)
    void initialize(std::string gameVersion, std::string loaderVersion);

    [[nodiscard]] bool isVersionCompatible() const { return mVersionCompatible; }
    [[nodiscard]] bool isHotReloadSupported() const { return mHotReloadSupported; }
    [[nodiscard]] bool isGraphicsSwitchSupported() const { return mGraphicsSwitchSupported; }

    [[nodiscard]] std::string const& detectedVersion() const { return mDetectedVersion; }
    [[nodiscard]] std::string const& detectedLoaderVersion() const { return mDetectedLoaderVersion; }
    [[nodiscard]] std::string_view   targetVersion() const { return "1.26.51.01"; }

    // Helpers de control explícito
    void setHotReloadSupported(bool supported) { mHotReloadSupported = supported; }
    void setGraphicsSwitchSupported(bool supported) { mGraphicsSwitchSupported = supported; }

    // Comprueba si una cadena de versión coincide con la versión objetivo (1.26.51.01 o prefijo 1.26.51)
    static bool isVersionMatching(std::string_view version, std::string_view target = "1.26.51.01");

    void reset();

private:
    CompatibilityManager();

    std::string mDetectedVersion       = "Unknown";
    std::string mDetectedLoaderVersion = "Unknown";
    bool        mVersionCompatible     = false;
    bool        mHotReloadSupported    = false;
    bool        mGraphicsSwitchSupported = false;
};

} // namespace aeris
