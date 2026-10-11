#pragma once

#include <filesystem>
#include <string>

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
        bool safeMode = false;
    } safety;
};

class ConfigManager {
public:
    static ConfigManager& getInstance();

    bool load(std::filesystem::path const& configFile);
    bool save() const;

    Config& config() { return mConfig; }
    Config const& config() const { return mConfig; }

    std::filesystem::path const& configPath() const { return mConfigPath; }

private:
    ConfigManager() = default;
    Config                mConfig;
    std::filesystem::path mConfigPath;
};

} // namespace aeris
