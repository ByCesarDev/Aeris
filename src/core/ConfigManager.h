#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace aeris {

struct Config {
    int         configVersion = 1;
    bool        enabled       = true;
    std::string language      = "es_ES";
    std::string dataDir       = "data";
    std::string shaderPacksDir = "shaderpacks";
    std::string generatedDir  = "generated";

    struct UiConfig {
        std::string shortcut = "G";
        float       backdrop = 0.0f;
        std::string theme    = "dark";
    } ui;

    struct ShadersConfig {
        std::string activePack = "";
        bool        hotReload  = true;
    } shaders;

    struct PerformanceConfig {
        bool diagnosticsEnabled = true;
    } performance;

    struct SafetyConfig {
        bool safeMode                    = true;  // Safe mode on by default
        bool enableExperimentalHooks     = false; // Native hooks disabled by default until verified
        bool enablePassiveReadHook       = false; // Passive readFile observation hook (Etapa 3.2, disabled by default)
        bool enableMaterialRedirectDryRun = false; // Dry-run simulation of material redirection (Etapa 3.3, disabled by default)
    } safety;
};

class ConfigManager {
public:
    static ConfigManager& getInstance();

    // Carga de forma transaccional. Si el archivo es inválido o no pasa la validación,
    // mConfig permanece intacto y la función devuelve false.
    bool load(std::filesystem::path const& configFile);

    // Valida exhaustivamente un objeto Config.
    static bool validate(Config const& cfg, std::string& outError);

    // Guarda atómicamente la configuración en disco usando archivo temporal y sustitución protegida.
    bool save() const;

    Config& config() { return mConfig; }
    Config const& config() const { return mConfig; }

    std::filesystem::path const& configPath() const { return mConfigPath; }
    std::string const& lastError() const { return mLastError; }

    // Reinicia a configuración por defecto
    void reset();

private:
    ConfigManager();

    mutable Config        mConfig;
    std::filesystem::path mConfigPath;
    mutable std::string   mLastError;
};

} // namespace aeris
